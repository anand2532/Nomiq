#include <stdio.h>

#include "image_fit.h"

static int fails, passes;
#define CHECK(c) do { if (c) passes++; else { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

/* JPEG size model: ~ pixels * bits-per-pixel(q); bpp grows roughly linearly with quality
 * (0.35 bpp at q=20 .. 2.0 bpp at q=90 for typical OV2640 scenes) times scene complexity. */
static double complexity = 1.0;
static int calls;
static unsigned model(img_res_t r, uint8_t q, void *ctx)
{
    (void)ctx; calls++;
    double bpp = 0.35 + (q - 20) * (1.65 / 70.0);
    return (unsigned)(r.w * r.h * bpp / 8.0 * complexity);
}
static unsigned broken(img_res_t r, uint8_t q, void *ctx) { (void)r; (void)q; (void)ctx; return 0; }

static long pct(long a, long b) { return (a - b) * 100 / b; }

int main(void)
{
    img_fit_cfg_t c; img_fit_default_cfg(&c);

    complexity = 1.0; calls = 0;
    img_fit_result_t r = img_fit(&c, model, NULL);
    CHECK(r.ok && r.bytes <= 12000 && IMG_RES[r.res_index].w == 320 && r.attempts <= 3);
    printf("  typical scene: %ux%u q%u %u B in %u captures\n", IMG_RES[r.res_index].w, IMG_RES[r.res_index].h, r.quality, r.bytes, r.attempts);

    complexity = 5.0;               /* very busy scene: must drop resolution */
    r = img_fit(&c, model, NULL);
    CHECK(r.ok && r.bytes <= 12000 && r.res_index > 2 && r.attempts <= c.max_attempts);
    printf("  busy scene:    %ux%u q%u %u B in %u captures\n", IMG_RES[r.res_index].w, IMG_RES[r.res_index].h, r.quality, r.bytes, r.attempts);

    complexity = 0.5; c.max_bytes = 40000; c.res_index = 0;   /* generous cap, VGA fits first time */
    r = img_fit(&c, model, NULL);
    CHECK(r.ok && r.res_index == 0 && r.attempts == 1);

    img_fit_default_cfg(&c); c.max_bytes = 100; complexity = 1.0;   /* impossible */
    r = img_fit(&c, model, NULL);
    CHECK(!r.ok && r.attempts <= c.max_attempts && r.bytes > 100);

    img_fit_default_cfg(&c);
    r = img_fit(&c, broken, NULL);
    CHECK(!r.ok && r.attempts == 1);

    /* ETA vs tools/lora_airtime.py (12 kB, 10 % PER): SF7 24477, SF9 76489, SF12 548810 ms */
    long e7 = xfer_eta_ms(12000, 394, 82, 10, 0);
    long e9 = xfer_eta_ms(12000, 1250, 267, 10, 0);
    long e12 = xfer_eta_ms(12000, 9019, 1974, 10, 0);
    printf("  ETA 12 kB: SF7 %ld ms, SF9 %ld ms, SF12 %ld ms\n", e7, e9, e12);
    CHECK(pct(e7, 24477) >= -5 && pct(e7, 24477) <= 5);
    CHECK(pct(e9, 76489) >= -5 && pct(e9, 76489) <= 5);
    CHECK(pct(e12, 548810) >= -5 && pct(e12, 548810) <= 5);
    long ed = xfer_eta_ms(12000, 1250, 267, 10, 25);   /* 2.5 % duty: ~2 991 627 ms */
    CHECK(pct(ed, 2991627) >= -5 && pct(ed, 2991627) <= 5);

    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
