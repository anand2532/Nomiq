/*
 * image_fit — choose JPEG resolution/quality so a photo fits the transfer budget
 * (REQ-CAM-002/004) and estimate its LoRa transfer time for the UI (REQ-RF-013, REQ-UI-004).
 * Pure C, host-tested; the capture callback is the camera driver on the device.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint16_t w, h; } img_res_t;

/* Resolutions supported by the OV2640 JPEG path, largest first */
#define IMG_RES_COUNT 5
extern const img_res_t IMG_RES[IMG_RES_COUNT];   /* 640x480 .. 96x96 */

typedef struct {
    uint8_t  res_index;     /* starting resolution (index into IMG_RES) */
    uint8_t  quality;       /* starting quality, 0..100 (higher = better) */
    uint8_t  min_quality;   /* do not go below this before downscaling */
    uint32_t max_bytes;     /* transfer cap */
    uint8_t  max_attempts;  /* bound on captures */
} img_fit_cfg_t;

/* Capture/encode at (res, quality), return JPEG size in bytes (0 on failure). */
typedef uint32_t (*img_capture_fn)(img_res_t res, uint8_t quality, void *ctx);

typedef struct {
    bool     ok;
    uint8_t  res_index;
    uint8_t  quality;
    uint32_t bytes;
    uint8_t  attempts;
} img_fit_result_t;

void img_fit_default_cfg(img_fit_cfg_t *c);   /* 320x240, q=60 -> min 20, 12 kB, 8 attempts */

/* Strategy: lower quality in steps (proportional to the overshoot) down to min_quality,
 * then step resolution down and restart at the original quality. Bounded by max_attempts. */
img_fit_result_t img_fit(const img_fit_cfg_t *cfg, img_capture_fn cap, void *ctx);

/* Transfer-time estimate in ms for an object of `bytes`, given the time-on-air of a full
 * (255 B) frame, of an ACK/SACK frame, packet-error rate (percent) and duty-cycle limit
 * (permille, 0 = none). Mirrors tools/lora_airtime.py. */
uint32_t xfer_eta_ms(uint32_t bytes, uint32_t toa_full_ms, uint32_t toa_ack_ms, uint8_t per_pct,
                     uint16_t duty_permille);

#ifdef __cplusplus
}
#endif
