/* camera_esp.c — OV2640 via espressif/esp32-camera. Pins: board_pins.h. Rails via IO expander.
 * NOTE: SCCB shares I2C0 with the expander/fuel gauge (sccb_i2c_port = 0). esp32-camera must be
 * built with its "new I2C driver" option when board.c uses i2c_master (IDF >= 5.3). Verify in
 * Phase 5 bring-up. */
#include "camera.h"

#include <stdio.h>
#include <string.h>
#include "board.h"
#include "board_pins.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "camera";
static bool s_on;
static pixformat_t s_fmt;
static camera_fb_t *s_last;
static camera_fb_t *s_preview;

static framesize_t to_framesize(img_res_t r)
{
    if (r.w >= 640) return FRAMESIZE_VGA;
    if (r.w >= 480) return FRAMESIZE_HVGA;
    if (r.w >= 320) return FRAMESIZE_QVGA;
    if (r.w >= 160) return FRAMESIZE_QQVGA;
    return FRAMESIZE_96X96;
}

static int to_jpeg_quality(uint8_t q) { return 63 - (int)q * 53 / 100; }   /* 100 -> 10 (best), 0 -> 63 */

static esp_err_t start(pixformat_t fmt)
{
    if (s_on && s_fmt == fmt) return ESP_OK;
    if (s_on) { esp_camera_deinit(); s_on = false; }
    camera_config_t c = {
        .pin_pwdn = -1, .pin_reset = -1,               /* via expander */
        .pin_xclk = PIN_CAM_XCLK,
        .pin_sccb_sda = -1, .pin_sccb_scl = -1, .sccb_i2c_port = 0,
        .pin_d7 = PIN_CAM_D7, .pin_d6 = PIN_CAM_D6, .pin_d5 = PIN_CAM_D5, .pin_d4 = PIN_CAM_D4,
        .pin_d3 = PIN_CAM_D3, .pin_d2 = PIN_CAM_D2, .pin_d1 = PIN_CAM_D1, .pin_d0 = PIN_CAM_D0,
        .pin_vsync = PIN_CAM_VSYNC, .pin_href = PIN_CAM_HREF, .pin_pclk = PIN_CAM_PCLK,
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_1, .ledc_channel = LEDC_CHANNEL_1,   /* timer/ch 0 = LCD backlight */
        .pixel_format = fmt,
        .frame_size = fmt == PIXFORMAT_JPEG ? FRAMESIZE_QVGA : FRAMESIZE_QVGA,
        .jpeg_quality = 20,
        .fb_count = 2,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST,
    };
    esp_err_t e = esp_camera_init(&c);
    if (e == ESP_OK) { s_on = true; s_fmt = fmt; }
    return e;
}

esp_err_t camera_power(bool on)
{
    if (on) {
        iox_set(IOX_CAM_PWR_EN, true);
        vTaskDelay(pdMS_TO_TICKS(5));
        iox_set(IOX_CAM_PWDN, false);
        iox_set(IOX_CAM_RESET, true);
        vTaskDelay(pdMS_TO_TICKS(20));
        return ESP_OK;
    }
    if (s_last) { esp_camera_fb_return(s_last); s_last = NULL; }
    camera_preview_release();
    if (s_on) { esp_camera_deinit(); s_on = false; }
    iox_set(IOX_CAM_RESET, false);
    iox_set(IOX_CAM_PWDN, true);
    return iox_set(IOX_CAM_PWR_EN, false);
}

static uint32_t capture_cb(img_res_t res, uint8_t q, void *ctx)
{
    (void)ctx;
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return 0;
    s->set_framesize(s, to_framesize(res));
    s->set_quality(s, to_jpeg_quality(q));
    if (s_last) { esp_camera_fb_return(s_last); s_last = NULL; }
    camera_fb_t *fb = esp_camera_fb_get();           /* discard: settings apply to next frame */
    if (fb) esp_camera_fb_return(fb);
    s_last = esp_camera_fb_get();
    if (!s_last || s_last->format != PIXFORMAT_JPEG) return 0;
    ESP_LOGI(TAG, "%ux%u q%u -> %u B", res.w, res.h, q, (unsigned)s_last->len);
    return (uint32_t)s_last->len;
}

esp_err_t camera_capture(const img_fit_cfg_t *cfg, const char *path, img_fit_result_t *out)
{
    camera_power(true);
    esp_err_t e = start(PIXFORMAT_JPEG);
    if (e != ESP_OK) return e;
    img_fit_result_t r = img_fit(cfg, capture_cb, NULL);
    if (out) *out = r;
    if (!r.ok || !s_last) return ESP_ERR_INVALID_SIZE;
    FILE *f = fopen(path, "wb");
    if (!f) return ESP_FAIL;
    size_t n = fwrite(s_last->buf, 1, s_last->len, f);
    fclose(f);
    esp_camera_fb_return(s_last);
    s_last = NULL;
    return n == r.bytes ? ESP_OK : ESP_FAIL;
}

esp_err_t camera_preview(const uint8_t **rgb565, size_t *len)
{
    esp_err_t e = start(PIXFORMAT_RGB565);
    if (e != ESP_OK) return e;
    camera_preview_release();
    s_preview = esp_camera_fb_get();
    if (!s_preview) return ESP_FAIL;
    *rgb565 = s_preview->buf;
    *len = s_preview->len;
    return ESP_OK;
}

void camera_preview_release(void)
{
    if (s_preview) { esp_camera_fb_return(s_preview); s_preview = NULL; }
}
