/* Voice codec tests: Codec2 round trip on a synthetic voiced signal (pitch & level preserved). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "voice_codec.h"

static int fails, passes;
#define CHECK(c) do { if (c) passes++; else { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

#define FS 16000
#define SECS 3

/* glottal pulse train through two formant resonators (vowel /a/-like) with slow F0 glide */
static void synth(int16_t *x, int n, double f0)
{
    double y1 = 0, y2 = 0, z1 = 0, z2 = 0, ph = 0;
    double r1 = 0.97, w1 = 2 * M_PI * 700 / FS, r2 = 0.96, w2 = 2 * M_PI * 1200 / FS;
    for (int i = 0; i < n; i++) {
        double f = f0 * (1.0 + 0.03 * sin(2 * M_PI * 0.5 * i / FS));
        ph += f / FS;
        double e = 0;
        if (ph >= 1.0) { ph -= 1.0; e = 1.0; }
        double a = e + 2 * r1 * cos(w1) * y1 - r1 * r1 * y2; y2 = y1; y1 = a;
        double b = a + 2 * r2 * cos(w2) * z1 - r2 * r2 * z2; z2 = z1; z1 = b;
        x[i] = (int16_t)(b * 180.0);
    }
}

static double rms(const int16_t *x, int a, int b) { double s = 0; for (int i = a; i < b; i++) s += (double)x[i] * x[i]; return sqrt(s / (b - a)); }

static double pitch(const int16_t *x, int a, int b)
{
    int best = 0; double bv = -1e30;
    for (int lag = FS / 400; lag <= FS / 60; lag++) {
        double s = 0;
        for (int i = a; i < b - lag; i++) s += (double)x[i] * x[i + lag];
        if (s > bv) { bv = s; best = lag; }
    }
    return (double)FS / best;
}

int main(void)
{
    int n = FS * SECS;
    int16_t *in = malloc(n * 2), *out = malloc((n + 4000) * 2);
    uint8_t *c2 = malloc(20000);
    synth(in, n, 120.0);
    double in_rms = rms(in, FS / 2, n), in_f0 = pitch(in, FS, FS + 4000);

    vc_mode_t modes[] = { VC_MODE_3200, VC_MODE_2400, VC_MODE_1200, VC_MODE_700C };
    for (unsigned m = 0; m < 4; m++) {
        clock_t t0 = clock();
        int len = vc_encode(modes[m], in, n, c2, 20000);
        double enc_ms = (clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
        CHECK(len > 0 && (uint32_t)len == vc_encoded_size(modes[m], SECS * 1000));
        double bps = (len - VC_HDR_LEN) * 8.0 / SECS;
        /* frames are byte-aligned: 700C (28 bit/frame) costs 800 bit/s on air */
        CHECK(fabs(bps - vc_bitrate(modes[m])) / vc_bitrate(modes[m]) < (modes[m] == VC_MODE_700C ? 0.15 : 0.05));
        int got = vc_decode(c2, len, out, n + 4000);
        CHECK(got >= n);
        double o_rms = rms(out, FS / 2, n), o_f0 = pitch(out, FS, FS + 4000);
        printf("  mode %4d bps: %4d B for %d s, decoded rms %.0f (in %.0f), F0 %.1f Hz (in %.1f), encode %.1f ms host\n",
               vc_bitrate(modes[m]), len, SECS, o_rms, in_rms, o_f0, in_f0, enc_ms);
        CHECK(o_rms > in_rms * 0.25 && o_rms < in_rms * 4.0);
        CHECK(fabs(o_f0 - in_f0) / in_f0 < 0.08);
    }
    /* 30 s at 1200 bps fits the link budget assumption (~4.5 kB) */
    CHECK(vc_encoded_size(VC_MODE_1200, 30000) == 8 + 750 * 6);

    /* corrupt header rejected */
    int len = vc_encode(VC_MODE_1200, in, n, c2, 20000);
    c2[0] = 'X'; CHECK(vc_decode(c2, len, out, n + 4000) < 0); c2[0] = 'N';
    CHECK(vc_decode(c2, 10, out, n + 4000) < 0);                 /* truncated */
    CHECK(vc_encode(VC_MODE_1200, in, n, c2, 100) < 0);          /* buffer too small */

    /* file round trip */
    FILE *f = fopen("/tmp/vc_in.pcm", "wb"); fwrite(in, 2, n, f); fclose(f);
    CHECK(vc_encode_file(VC_MODE_1200, "/tmp/vc_in.pcm", "/tmp/vc.c2") == len);
    CHECK(vc_decode_file("/tmp/vc.c2", "/tmp/vc_out.pcm") >= n);
    remove("/tmp/vc_in.pcm"); remove("/tmp/vc.c2"); remove("/tmp/vc_out.pcm");

    free(in); free(out); free(c2);
    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
