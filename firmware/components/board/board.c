/* board.c — bus bring-up and TCA9535 expander driver (ESP-IDF >= 5.3, new I2C master API). */
#include "board.h"
#include "board_pins.h"

#include "esp_check.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

/* TCA9535 registers */
#define TCA_IN0   0x00
#define TCA_OUT0  0x02
#define TCA_CFG0  0x06

/* direction: 1 = input. Port0 = buttons/status (inputs), Port1 = controls (outputs) */
#define IOX_DIR_MASK  0x00FFu
/* safe defaults: LORA_NRST high (not in reset), LCD_RST high, CAM_PWDN high (powered down),
 * CAM_RESET low, AMP_SD low (amp off), STT_EN low, CAM_PWR_EN low */
#define IOX_OUT_INIT  ((1u << IOX_LORA_NRST) | (1u << IOX_LCD_RST) | (1u << IOX_CAM_PWDN))

static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_iox;
static uint16_t s_out = IOX_OUT_INIT;
static bool s_ready;

static __attribute__((unused)) esp_err_t tca_write16(uint8_t reg, uint16_t v)
{
    uint8_t b[3] = { reg, (uint8_t)v, (uint8_t)(v >> 8) };
    return i2c_master_transmit(s_iox, b, sizeof b, 50);
}

esp_err_t board_init(void)
{
    if (s_ready) return ESP_OK;

    i2c_master_bus_config_t bc = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,   /* external 2.2 k */
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bc, &s_i2c), TAG, "i2c bus");

#if CONFIG_NOMIQ_BOARD_PCB_A0
    i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = IOX_ADDR, .scl_speed_hz = 400000 };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &dc, &s_iox), TAG, "iox add");
    ESP_RETURN_ON_ERROR(tca_write16(TCA_OUT0, s_out), TAG, "iox out");   /* set levels before enabling outputs */
    ESP_RETURN_ON_ERROR(tca_write16(TCA_CFG0, IOX_DIR_MASK), TAG, "iox cfg");
#else
    gpio_config_t g = { .pin_bit_mask = 1ULL << CONFIG_NOMIQ_DEVKIT_LORA_NRST_GPIO, .mode = GPIO_MODE_OUTPUT };
    ESP_RETURN_ON_ERROR(gpio_config(&g), TAG, "nrst gpio");
    gpio_set_level(CONFIG_NOMIQ_DEVKIT_LORA_NRST_GPIO, 1);
#endif

    spi_bus_config_t sb = {
        .mosi_io_num = PIN_SPI_MOSI,
        .miso_io_num = PIN_SPI_MISO,
        .sclk_io_num = PIN_SPI_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 320 * 40 * 2,   /* LCD partial-frame flushes */
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_SPI_HOST, &sb, SPI_DMA_CH_AUTO), TAG, "spi bus");

    s_ready = true;
    ESP_LOGI(TAG, "board ready, device id %08lx", (unsigned long)board_device_id());
    return ESP_OK;
}

i2c_master_bus_handle_t board_i2c_bus(void) { return s_i2c; }

esp_err_t iox_set(uint8_t bit, bool level)
{
#if !CONFIG_NOMIQ_BOARD_PCB_A0
    if (bit == IOX_LORA_NRST) return gpio_set_level(CONFIG_NOMIQ_DEVKIT_LORA_NRST_GPIO, level);
    return ESP_OK;   /* dev kit: no expander, other controls not fitted */
#endif
    if (bit < 8 || bit > 15) return ESP_ERR_INVALID_ARG;   /* only port1 is output */
    uint16_t v = level ? (uint16_t)(s_out | (1u << bit)) : (uint16_t)(s_out & ~(1u << bit));
    esp_err_t e = tca_write16(TCA_OUT0, v);
    if (e == ESP_OK) s_out = v;
    return e;
}

esp_err_t iox_read(uint16_t *inputs)
{
#if !CONFIG_NOMIQ_BOARD_PCB_A0
    *inputs = 0x003F;   /* dev kit: no buttons -> all released (active low) */
    return ESP_OK;
#endif
    uint8_t reg = TCA_IN0, b[2];
    esp_err_t e = i2c_master_transmit_receive(s_iox, &reg, 1, b, 2, 50);
    if (e == ESP_OK) *inputs = (uint16_t)(b[0] | (b[1] << 8));
    return e;
}

void board_lora_reset(void)
{
    iox_set(IOX_LORA_NRST, false);
    vTaskDelay(pdMS_TO_TICKS(2));
    iox_set(IOX_LORA_NRST, true);
    vTaskDelay(pdMS_TO_TICKS(10));
}

uint32_t board_device_id(void)
{
    uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);
    /* FNV-1a over the MAC: stable, well-mixed, never 0 or broadcast */
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; i++) { h ^= mac[i]; h *= 16777619u; }
    if (h == 0 || h == 0xFFFFFFFFu) h ^= 0x5A5A5A5Au;
    return h;
}
