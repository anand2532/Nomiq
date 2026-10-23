/* image_fit.c — see include/image_fit.h */
#include "image_fit.h"

const img_res_t IMG_RES[IMG_RES_COUNT] = {
    { 640, 480 }, { 480, 320 }, { 320, 240 }, { 160, 120 }, { 96, 96 },
};

#define FRAG_PAYLOAD 223u
#define WINDOW       16u

void img_fit_default_cfg(img_fit_cfg_t *c)
{
    c->res_index = 2;        /* 320x240 */
    c->quality = 60;
    c->min_quality = 20;
    c->max_bytes = 12000;
    c->max_attempts = 8;
}

img_fit_result_t img_fit(const img_fit_cfg_t *cfg, img_capture_fn cap, void *ctx)
{
    img_fit_result_t r = { .ok = false, .res_index = cfg->res_index, .quality = cfg->quality };
    uint8_t res = cfg->res_index, q = cfg->quality;
    while (r.attempts < cfg->max_attempts && res < IMG_RES_COUNT) {
        uint32_t size = cap(IMG_RES[res], q, ctx);
        r.attempts++;
        if (size == 0) return r;                       /* camera error */
        r.res_index = res; r.quality = q; r.bytes = size;
        if (size <= cfg->max_bytes) { r.ok = true; return r; }
        if (q > cfg->min_quality) {
            /* step proportional to overshoot: 2x too big -> drop ~half the remaining range */
            uint32_t over_pct = (size - cfg->max_bytes) * 100u / size;   /* 0..99 */
            uint32_t step = (uint32_t)(q - cfg->min_quality) * over_pct / 50u;
            if (step < 8) step = 8;
            q = (q > cfg->min_quality + step) ? (uint8_t)(q - step) : cfg->min_quality;
        } else {
            res++;
            q = cfg->quality;
        }
    }
    return r;
}

uint32_t xfer_eta_ms(uint32_t bytes, uint32_t toa_full_ms, uint32_t toa_ack_ms, uint8_t per_pct,
                     uint16_t duty_permille)
{
    if (per_pct > 90) per_pct = 90;
    uint32_t frags = bytes ? (bytes + FRAG_PAYLOAD - 1) / FRAG_PAYLOAD : 1;
    uint32_t windows = (frags + WINDOW - 1) / WINDOW;
    uint64_t tx = (uint64_t)frags * toa_full_ms * 100u / (100u - per_pct);
    uint64_t rx = (uint64_t)windows * toa_ack_ms * 100u / (100u - per_pct);
    uint64_t wall = tx + rx + windows * 60u + frags * 5u;
    if (duty_permille) {
        uint64_t limited = tx * 1000u / duty_permille;
        if (limited > wall) wall = limited;
    }
    return wall > UINT32_MAX ? UINT32_MAX : (uint32_t)wall;
}
