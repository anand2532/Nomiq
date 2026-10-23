/* stt_link_esp.c — UART link to the power-gated STT co-processor (tools/stt-coproc). */
#include "stt_link.h"

#include <stdio.h>
#include <string.h>
#include "board_pins.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power.h"
#include "stt_proto.h"

static const char *TAG = "stt";
#define STT_UART UART_NUM_0          /* console runs on USB-Serial-JTAG, UART0 is free */
static bool s_uart;
static stt_parser_t s_p;             /* 4 kB+: static */
static uint8_t s_tx[STT_MAX_PAYLOAD + STT_OVERHEAD];

static esp_err_t uart_up(void)
{
    if (s_uart) return ESP_OK;
    uart_config_t c = { .baud_rate = 921600, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE,
                        .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT };
    esp_err_t e = uart_driver_install(STT_UART, 8192, 8192, 0, NULL, 0);
    if (e == ESP_OK) e = uart_param_config(STT_UART, &c);
    if (e == ESP_OK) e = uart_set_pin(STT_UART, PIN_STT_TX, PIN_STT_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    s_uart = e == ESP_OK;
    return e;
}

static int send_frame(uint8_t type, const uint8_t *p, uint32_t n)
{
    int len = stt_encode(type, p, n, s_tx, sizeof s_tx);
    return len > 0 && uart_write_bytes(STT_UART, s_tx, (size_t)len) == len ? 0 : -1;
}

/* wait for a frame of one of the given types; returns its type or -1 on timeout */
static int wait_frame(uint8_t t1, uint8_t t2, uint8_t t3, uint32_t timeout_ms)
{
    int64_t until = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    uint8_t b[256];
    while (esp_timer_get_time() < until) {
        int n = uart_read_bytes(STT_UART, b, sizeof b, pdMS_TO_TICKS(50));
        for (int i = 0; i < n; i++) {
            if (stt_parser_feed(&s_p, b[i]) && (s_p.type == t1 || s_p.type == t2 || s_p.type == t3)) return s_p.type;
        }
    }
    return -1;
}

esp_err_t stt_power_on(uint32_t boot_timeout_ms)
{
    if (uart_up() != ESP_OK) return ESP_FAIL;
    stt_parser_init(&s_p);
    uart_flush_input(STT_UART);
    if (power_rail(RAIL_STT, true) != PWR_OK) return ESP_ERR_INVALID_STATE;
    int64_t t0 = esp_timer_get_time();
    /* READY is sent unsolicited at boot; HELLO re-asks if we missed it */
    int t = wait_frame(STT_READY, STT_READY, STT_READY, boot_timeout_ms);
    if (t < 0) { send_frame(STT_HELLO, NULL, 0); t = wait_frame(STT_READY, STT_READY, STT_READY, 2000); }
    if (t < 0) { power_rail(RAIL_STT, false); ESP_LOGW(TAG, "co-processor did not report READY"); return ESP_ERR_TIMEOUT; }
    ESP_LOGI(TAG, "READY (%.*s) after %lld ms", (int)s_p.len, (const char *)s_p.payload, (esp_timer_get_time() - t0) / 1000);
    return ESP_OK;
}

esp_err_t stt_transcribe_file(const char *pcm_path, const char *lang, stt_result_t *out)
{
    FILE *f = fopen(pcm_path, "rb");
    if (!f) return ESP_ERR_NOT_FOUND;
    int64_t t0 = esp_timer_get_time();
    uint8_t begin[12] = { 0x80, 0x3E, 0, 0 };          /* 16000 LE */
    strncpy((char *)&begin[4], lang ? lang : "en", 8);
    send_frame(STT_AUDIO_BEGIN, begin, sizeof begin);
    static uint8_t chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) send_frame(STT_AUDIO_CHUNK, chunk, (uint32_t)n);
    fclose(f);
    send_frame(STT_AUDIO_END, NULL, 0);
    int t = wait_frame(STT_FINAL, STT_ERROR, STT_ERROR, 60000);
    out->latency_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    if (t == STT_FINAL) {
        stt_final_decode(s_p.payload, s_p.len, out->text, sizeof out->text, &out->confidence);
        return ESP_OK;
    }
    out->text[0] = 0;
    out->confidence = 0;
    return t == STT_ERROR ? ESP_FAIL : ESP_ERR_TIMEOUT;
}

esp_err_t stt_power_off(void)
{
    send_frame(STT_SHUTDOWN, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(3000));                    /* let Linux sync the filesystem */
    power_rail(RAIL_STT, false);
    return ESP_OK;
}
