/* Camera: OV2640 capture with automatic JPEG size control (Phase 5). Logic: image_fit.h */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "image_fit.h"

esp_err_t camera_power(bool on);
/* Capture a photo that fits cfg->max_bytes and write it as JPEG to `path`. */
esp_err_t camera_capture(const img_fit_cfg_t *cfg, const char *path, img_fit_result_t *out);
/* Grab an RGB565 preview frame (320x240) for the LCD; release with camera_preview_release(). */
esp_err_t camera_preview(const uint8_t **rgb565, size_t *len);
void      camera_preview_release(void);
