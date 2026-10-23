/*
 * Nomiq firmware — Phase 1 application: device-to-device text link with ACK/retry.
 *
 * Both units run the same image. Set CONFIG_NOMIQ_PEER_ID (idf.py menuconfig) to the other
 * unit's ID to send periodic pings; every completed ping prints one CSV line for the
 * range/PER log (tests/plans/master-test-plan.md TP-RF-01/05):
 *   RANGE,<uptime_ms>,<seq>,<delivered>,<retries>,<frames>,<peer_snr>,<peer_rssi>,<latency_ms>
 * Received texts print as:  TEXT,<uptime_ms>,<src>,<rssi>,<snr>,"<text>"
 */
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "board.h"
#include "board_pins.h"
#include "nomiq_proto.h"
#include "nomiq_xfer.h"
#include "radio.h"

static const char *TAG = "nomiq";

typedef struct { uint8_t len; int16_t rssi; int8_t snr; uint8_t buf[255]; } rx_item_t;

static QueueHandle_t s_rxq;
static uint32_t s_self;
static nomiq_rx_t s_rx;
static nomiq_tx_t s_ping;
static int16_t s_last_rssi;
static int8_t s_last_snr;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void on_radio_rx(const uint8_t *frame, size_t len, radio_link_t link, void *ctx)
{
    (void)ctx;
    rx_item_t it = { .len = (uint8_t)len, .rssi = link.rssi_dbm, .snr = link.snr_db };
    memcpy(it.buf, frame, len);
    if (xQueueSend(s_rxq, &it, 0) != pdTRUE) ESP_LOGW(TAG, "rx queue full, frame dropped");
}

static uint32_t io_send(const uint8_t *frame, size_t len, void *ctx)
{
    (void)ctx;
    uint32_t toa = radio_time_on_air_ms(len);
    esp_err_t e = radio_send(frame, len, 0);
    if (e == ESP_ERR_NOT_ALLOWED) ESP_LOGW(TAG, "duty-cycle budget exhausted, frame deferred");
    else if (e != ESP_OK) ESP_LOGW(TAG, "radio_send: %s", esp_err_to_name(e));
    return toa;
}

static void io_on_text(uint32_t src, const uint8_t *txt, uint8_t len, void *ctx)
{
    (void)ctx;
    printf("TEXT,%lu,%08lx,%d,%d,\"%.*s\"\n", (unsigned long)now_ms(), (unsigned long)src,
           s_last_rssi, s_last_snr, len, (const char *)txt);
}

static const nomiq_io_t s_io = { .send = io_send, .on_text = io_on_text };

static void app_task(void *arg)
{
    (void)arg;
    uint32_t seq = 0, next_ping = now_ms() + 3000, ping_started = 0;
    char ping_txt[40];
    bool ping_active = false;
    nomiq_tx_t *txs[1] = { &s_ping };
    rx_item_t it;

    for (;;) {
        while (xQueueReceive(s_rxq, &it, pdMS_TO_TICKS(10)) == pdTRUE) {
            nomiq_hdr_t h; const uint8_t *pl;
            int rc = nomiq_frame_decode(it.buf, it.len, &h, &pl);
            if (rc != NOMIQ_OK) { ESP_LOGD(TAG, "drop frame (%d)", rc); continue; }
            if (h.dst != s_self) continue;
            s_last_rssi = it.rssi; s_last_snr = it.snr;
            if (ping_active && nomiq_tx_on_frame(&s_ping, &h, pl, now_ms())) continue;
            nomiq_rx_on_frame(&s_rx, &h, pl, it.snr, (int8_t)(it.rssi < -128 ? -128 : it.rssi), now_ms(), &s_io);
        }

        uint32_t t = now_ms();
        if (ping_active) {
            int pick = nomiq_sched_pick(txs, 1, t);
            nomiq_tx_poll(&s_ping, t, pick == 0, &s_io);
            if (nomiq_tx_done(&s_ping)) {
                printf("RANGE,%lu,%lu,%d,%u,%lu,%d,%d,%lu\n", (unsigned long)t, (unsigned long)seq,
                       s_ping.state == NOMIQ_TX_DELIVERED, s_ping.retries, (unsigned long)s_ping.frames_sent,
                       s_ping.peer_snr, s_ping.peer_rssi, (unsigned long)(t - ping_started));
                ping_active = false;
            }
        } else if (CONFIG_NOMIQ_PEER_ID != 0 && (int32_t)(t - next_ping) >= 0) {
            seq++;
            int n = snprintf(ping_txt, sizeof ping_txt, "ping %lu from %08lx", (unsigned long)seq, (unsigned long)s_self);
            nomiq_tx_begin_text(&s_ping, s_self, CONFIG_NOMIQ_PEER_ID, (uint16_t)seq, (const uint8_t *)ping_txt, (uint8_t)n);
            ping_active = true;
            ping_started = t;
            next_ping = t + CONFIG_NOMIQ_PING_INTERVAL_MS;
        }
        nomiq_rx_poll(&s_rx, t);
    }
}

void app_main(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_ERROR_CHECK(board_init());
    s_self = board_device_id();
    ESP_LOGI(TAG, "Nomiq phase-1 link test — device id %08lx, peer %08lx, proto v%u",
             (unsigned long)s_self, (unsigned long)CONFIG_NOMIQ_PEER_ID, NOMIQ_PROTO_VERSION);

    s_rxq = xQueueCreate(16, sizeof(rx_item_t));
    nomiq_rx_engine_init(&s_rx, s_self);

    radio_cfg_t rc = {
        .freq_hz = CONFIG_NOMIQ_LORA_FREQ_HZ,
        .sf = CONFIG_NOMIQ_LORA_SF,
        .bw_khz = LORA_BW_KHZ,
        .cr = 1,
        .tx_dbm = CONFIG_NOMIQ_TX_DBM,
#if CONFIG_NOMIQ_REG_PROFILE_SRD2021
        .reg_profile = REG_PROFILE_IN865_SRD2021,
#else
        .reg_profile = REG_PROFILE_IN865_2008,
#endif
    };
    ESP_ERROR_CHECK(radio_init(&rc, on_radio_rx, NULL));
    printf("CSV header: RANGE,uptime_ms,seq,delivered,retries,frames,peer_snr,peer_rssi,latency_ms\n");
    xTaskCreatePinnedToCore(app_task, "app", 6144, NULL, 5, NULL, 0);
}
