/* audio_esp.c — ESP-IDF I2S (std mode, full duplex on I2S0) for ICS-43434 + MAX98357A. */
#include "audio.h"

#include <stdio.h>
#include <string.h>
#include "board.h"
#include "board_pins.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "audio";
static i2s_chan_handle_t s_tx, s_rx;
static volatile bool s_rec_run, s_play_run;
static SemaphoreHandle_t s_rec_done, s_play_done;
static char s_rec_path[96], s_play_path[96];
static uint32_t s_rec_max_ms, s_rec_samples;

#define FRAME 256   /* samples per I2S read/write */

esp_err_t audio_init(void)
{
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.dma_frame_num = FRAME;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&cc, &s_tx, &s_rx), TAG, "channel");
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED, .bclk = PIN_I2S_BCLK, .ws = PIN_I2S_WS,
            .dout = PIN_I2S_DOUT, .din = PIN_I2S_DIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    std.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;   /* mic LR=GND -> left; amp plays left (SD_MODE high) */
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std), TAG, "tx");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &std), TAG, "rx");
    s_rec_done = xSemaphoreCreateBinary();
    s_play_done = xSemaphoreCreateBinary();
    return ESP_OK;
}

static void rec_task(void *arg)
{
    (void)arg;
    FILE *f = fopen(s_rec_path, "wb");
    static int32_t raw[FRAME];
    static int16_t pcm[FRAME];
    int32_t hp_x = 0, hp_y = 0;                 /* DC-blocking high-pass, ~10 Hz */
    uint32_t max_samples = s_rec_max_ms * (AUDIO_SAMPLE_RATE / 1000);
    s_rec_samples = 0;
    i2s_channel_enable(s_rx);
    while (f && s_rec_run && s_rec_samples < max_samples) {
        size_t got = 0;
        if (i2s_channel_read(s_rx, raw, sizeof raw, &got, 100) != ESP_OK) continue;
        size_t n = got / 4;
        for (size_t i = 0; i < n; i++) {
            int32_t x = raw[i] >> 14;               /* 24-bit MSB-aligned -> 18-bit, i.e. +12 dB over >>16 */
            int32_t y = x - hp_x + ((hp_y * 32604) >> 15);
            hp_x = x; hp_y = y;
            pcm[i] = (int16_t)(y > 32767 ? 32767 : y < -32768 ? -32768 : y);
        }
        fwrite(pcm, 2, n, f);
        s_rec_samples += n;
    }
    i2s_channel_disable(s_rx);
    if (f) fclose(f);
    s_rec_run = false;
    xSemaphoreGive(s_rec_done);
    vTaskDelete(NULL);
}

esp_err_t audio_record_start(const char *pcm_path, uint32_t max_ms)
{
    if (s_rec_run) return ESP_ERR_INVALID_STATE;
    snprintf(s_rec_path, sizeof s_rec_path, "%s", pcm_path);
    s_rec_max_ms = max_ms;
    s_rec_run = true;
    xSemaphoreTake(s_rec_done, 0);
    return xTaskCreatePinnedToCore(rec_task, "rec", 4096, NULL, 10, NULL, 1) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t audio_record_stop(uint32_t *duration_ms)
{
    if (s_rec_run) { s_rec_run = false; xSemaphoreTake(s_rec_done, pdMS_TO_TICKS(2000)); }
    if (duration_ms) *duration_ms = s_rec_samples / (AUDIO_SAMPLE_RATE / 1000);
    return ESP_OK;
}

bool audio_is_recording(void) { return s_rec_run; }

esp_err_t audio_encode(const char *pcm_path, const char *c2_path, vc_mode_t mode)
{
    return vc_encode_file(mode, pcm_path, c2_path) > 0 ? ESP_OK : ESP_FAIL;
}

static void play_task(void *arg)
{
    (void)arg;
    char path[104];
    snprintf(path, sizeof path, "%s", s_play_path);
    size_t l = strlen(path);
    if (l > 3 && !strcmp(path + l - 3, ".c2")) {        /* decode to a temp PCM first */
        snprintf(path, sizeof path, "%s.pcm", s_play_path);
        if (vc_decode_file(s_play_path, path) <= 0) path[0] = 0;
    }
    FILE *f = path[0] ? fopen(path, "rb") : NULL;
    static int16_t pcm[FRAME];
    static int32_t out[FRAME];
    iox_set(IOX_AMP_SD, true);
    i2s_channel_enable(s_tx);
    size_t n;
    while (f && s_play_run && (n = fread(pcm, 2, FRAME, f)) > 0) {
        for (size_t i = 0; i < n; i++) out[i] = (int32_t)pcm[i] << 16;
        size_t w;
        i2s_channel_write(s_tx, out, n * 4, &w, 500);
    }
    memset(out, 0, sizeof out);
    size_t w;
    i2s_channel_write(s_tx, out, sizeof out, &w, 100);   /* flush silence: no pop */
    i2s_channel_disable(s_tx);
    iox_set(IOX_AMP_SD, false);
    if (f) fclose(f);
    if (strcmp(path, s_play_path) != 0 && path[0]) remove(path);
    s_play_run = false;
    xSemaphoreGive(s_play_done);
    vTaskDelete(NULL);
}

esp_err_t audio_play(const char *path)
{
    if (s_play_run) audio_stop();
    snprintf(s_play_path, sizeof s_play_path, "%s", path);
    s_play_run = true;
    xSemaphoreTake(s_play_done, 0);
    return xTaskCreatePinnedToCore(play_task, "play", 6144, NULL, 9, NULL, 1) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t audio_stop(void)
{
    if (s_play_run) { s_play_run = false; xSemaphoreTake(s_play_done, pdMS_TO_TICKS(2000)); }
    return ESP_OK;
}

bool audio_is_playing(void) { return s_play_run; }
