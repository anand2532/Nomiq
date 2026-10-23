/* Camera: OV2640 capture + JPEG size control. Phase 5. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
typedef struct { uint16_t width, height; uint8_t quality; uint32_t max_bytes; } camera_shot_cfg_t;
esp_err_t camera_init(void);
esp_err_t camera_power(bool on);
/* Captures, then re-encodes / down-scales until size <= max_bytes. Writes JPEG to path. */
esp_err_t camera_capture(const camera_shot_cfg_t *cfg, const char *path, size_t *out_len);
