/* radio_esp.c — ESP-IDF glue: SPI device on the shared bus, DIO1 interrupt, RX task,
 * blocking TX with duty-cycle accounting. Implements include/radio.h. */
#include "radio.h"

#include <string.h>
#include "board.h"
#include "board_pins.h"
#include "sx126x.h"
#include "nomiq_xfer.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "radio";

static spi_device_handle_t s_spi;
static sx126x_t s_dev;
static radio_cfg_t s_cfg;
static radio_rx_cb_t s_cb;
static void *s_cb_ctx;
static SemaphoreHandle_t s_lock;      /* serialises chip access */
static SemaphoreHandle_t s_irq_sem;   /* DIO1 */
static SemaphoreHandle_t s_tx_done;
static volatile bool s_tx_active;
static nomiq_duty_t s_duty;

/* ---------- HAL ---------- */

static int hal_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t n)
{
    (void)ctx;
    spi_transaction_t t = { .length = n * 8, .tx_buffer = tx, .rx_buffer = rx };
    return spi_device_polling_transmit(s_spi, &t) == ESP_OK ? 0 : -1;
}

static int hal_wait_busy(void *ctx, uint32_t timeout_ms)
{
    (void)ctx;
    int64_t until = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (gpio_get_level(PIN_LORA_BUSY)) {
        if (esp_timer_get_time() > until) return -1;
        if (timeout_ms > 2) vTaskDelay(1); else esp_rom_delay_us(10);
    }
    return 0;
}

static void hal_reset(void *ctx) { (void)ctx; board_lora_reset(); }
static void hal_delay(void *ctx, uint32_t ms) { (void)ctx; vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1)); }

static const sx126x_hal_t s_hal = { hal_xfer, hal_wait_busy, hal_reset, hal_delay, NULL };

static void IRAM_ATTR dio1_isr(void *arg)
{
    (void)arg;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_irq_sem, &hp);
    if (hp) portYIELD_FROM_ISR();
}

/* ---------- RX task ---------- */

static void radio_task(void *arg)
{
    (void)arg;
    uint8_t buf[255];   /* SX1262 max payload; sx126x_read_packet takes a uint8_t length */
    for (;;) {
        xSemaphoreTake(s_irq_sem, pdMS_TO_TICKS(1000));
        xSemaphoreTake(s_lock, portMAX_DELAY);
        uint16_t irq = 0;
        if (sx126x_irq_status(&s_dev, &irq) != 0 || irq == 0) { xSemaphoreGive(s_lock); continue; }
        sx126x_irq_clear(&s_dev, irq);

        if ((irq & SX_IRQ_TX_DONE) || ((irq & SX_IRQ_TIMEOUT) && s_tx_active)) {
            s_tx_active = false;
            xSemaphoreGive(s_tx_done);
            xSemaphoreGive(s_lock);
            continue;
        }
        if (irq & SX_IRQ_RX_DONE) {
            uint8_t len = 0; int16_t rssi = 0; int8_t snr = 0;
            bool bad = irq & (SX_IRQ_CRC_ERR | SX_IRQ_HEADER_ERR);
            if (!bad && sx126x_read_packet(&s_dev, buf, sizeof buf, &len, &rssi, &snr) == 0 && len) {
                xSemaphoreGive(s_lock);
                if (s_cb) s_cb(buf, len, (radio_link_t){ .rssi_dbm = rssi, .snr_db = snr }, s_cb_ctx);
                continue;
            }
        }
        xSemaphoreGive(s_lock);
    }
}

/* ---------- radio.h API ---------- */

static void to_sx_cfg(const radio_cfg_t *c, sx126x_cfg_t *x)
{
    sx126x_default_cfg(x);
    x->freq_hz = c->freq_hz;
    x->sf = c->sf;
    x->bw_khz = c->bw_khz;
    x->cr = c->cr;
    x->tx_dbm = c->tx_dbm;
#if CONFIG_NOMIQ_BOARD_DEVKIT
    x->tcxo = CONFIG_NOMIQ_DEVKIT_TCXO;
#endif
}

esp_err_t radio_init(const radio_cfg_t *cfg, radio_rx_cb_t cb, void *ctx)
{
    ESP_RETURN_ON_ERROR(board_init(), TAG, "board");
    if (cfg->bw_khz > 125) {
        ESP_LOGE(TAG, "bandwidth %u kHz not permitted in India (<= 200 kHz carrier); using 125", cfg->bw_khz);
    }
    s_cfg = *cfg;
    if (s_cfg.bw_khz > 125) s_cfg.bw_khz = 125;
    s_cb = cb; s_cb_ctx = ctx;
    s_lock = xSemaphoreCreateMutex();
    s_irq_sem = xSemaphoreCreateBinary();
    s_tx_done = xSemaphoreCreateBinary();
    nomiq_duty_init(&s_duty, s_cfg.reg_profile == REG_PROFILE_IN865_SRD2021 ? 25 : 0);

    spi_device_interface_config_t dc = {
        .clock_speed_hz = 8 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_LORA_NSS,
        .queue_size = 2,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(BOARD_SPI_HOST, &dc, &s_spi), TAG, "spi dev");

    gpio_config_t in = { .pin_bit_mask = (1ULL << PIN_LORA_BUSY), .mode = GPIO_MODE_INPUT };
    ESP_RETURN_ON_ERROR(gpio_config(&in), TAG, "busy");
    gpio_config_t irq = { .pin_bit_mask = (1ULL << PIN_LORA_DIO1), .mode = GPIO_MODE_INPUT,
                          .intr_type = GPIO_INTR_POSEDGE };
    ESP_RETURN_ON_ERROR(gpio_config(&irq), TAG, "dio1");
    esp_err_t e = gpio_install_isr_service(0);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(PIN_LORA_DIO1, dio1_isr, NULL), TAG, "isr");

    sx126x_cfg_t x;
    to_sx_cfg(&s_cfg, &x);
    if (sx126x_init(&s_dev, &s_hal, &x) != 0) {
        ESP_LOGE(TAG, "SX1262 init failed (err %d) — check wiring/BUSY/NRESET", s_dev.last_err);
        return ESP_FAIL;
    }
    uint16_t errs = 0;
    sx126x_device_errors(&s_dev, &errs);
    if (errs) ESP_LOGW(TAG, "SX1262 device errors 0x%04x", errs);

    xTaskCreatePinnedToCore(radio_task, "radio", 4096, NULL, configMAX_PRIORITIES - 2, NULL, 1);
    ESP_LOGI(TAG, "SX1262 up: %lu Hz SF%u BW%u CR4/%u %+d dBm, profile %s",
             (unsigned long)s_cfg.freq_hz, s_cfg.sf, s_cfg.bw_khz, s_cfg.cr + 4, s_cfg.tx_dbm,
             s_cfg.reg_profile == REG_PROFILE_IN865_SRD2021 ? "IN865_SRD2021 (2.5%)" : "IN865_2008");
    return radio_start_rx();
}

esp_err_t radio_set_cfg(const radio_cfg_t *cfg)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int rc = 0;
    rc |= sx126x_standby(&s_dev, false);
    if (cfg->freq_hz != s_cfg.freq_hz) rc |= sx126x_set_frequency(&s_dev, cfg->freq_hz);
    rc |= sx126x_set_lora(&s_dev, cfg->sf, cfg->bw_khz > 125 ? 125 : cfg->bw_khz, cfg->cr);
    rc |= sx126x_set_tx_power(&s_dev, cfg->tx_dbm);
    s_cfg = *cfg;
    if (s_cfg.bw_khz > 125) s_cfg.bw_khz = 125;
    s_duty.permille = s_cfg.reg_profile == REG_PROFILE_IN865_SRD2021 ? 25 : 0;
    rc |= sx126x_receive_continuous(&s_dev);
    xSemaphoreGive(s_lock);
    return rc ? ESP_FAIL : ESP_OK;
}

uint32_t radio_time_on_air_ms(size_t len)
{
    return (sx126x_toa_us(&s_dev.cfg, (uint8_t)len) + 999u) / 1000u;
}

uint32_t radio_duty_budget_ms(void)
{
    if (!s_duty.permille) return UINT32_MAX;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t budget = 3600u * s_duty.permille, used = nomiq_duty_used_ms(&s_duty, now);
    return used >= budget ? 0 : budget - used;
}

esp_err_t radio_send(const uint8_t *frame, size_t len, uint32_t timeout_ms)
{
    if (len > 255) return ESP_ERR_INVALID_SIZE;
    uint32_t toa = radio_time_on_air_ms(len);
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (!nomiq_duty_can_send(&s_duty, now, toa)) return ESP_ERR_NOT_ALLOWED;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    xSemaphoreTake(s_tx_done, 0);   /* drain */
    s_tx_active = true;
    int rc = sx126x_transmit(&s_dev, frame, (uint8_t)len, toa * 2 + 100);
    xSemaphoreGive(s_lock);
    if (rc != 0) { s_tx_active = false; return ESP_FAIL; }

    bool ok = xSemaphoreTake(s_tx_done, pdMS_TO_TICKS(timeout_ms ? timeout_ms : toa * 2 + 200)) == pdTRUE;
    nomiq_duty_record(&s_duty, now, toa);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    sx126x_receive_continuous(&s_dev);
    xSemaphoreGive(s_lock);
    return ok ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t radio_start_rx(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int rc = sx126x_receive_continuous(&s_dev);
    xSemaphoreGive(s_lock);
    return rc ? ESP_FAIL : ESP_OK;
}

esp_err_t radio_sleep(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int rc = sx126x_sleep(&s_dev, true);
    xSemaphoreGive(s_lock);
    return rc ? ESP_FAIL : ESP_OK;
}
