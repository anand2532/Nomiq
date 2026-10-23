/* voice_codec.c — see include/voice_codec.h */
#include "voice_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codec2.h"

/* 31-tap half-band low-pass (fc ≈ 3.6 kHz @ 16 kHz), windowed-sinc, Q15 */
#define NTAPS 31
static int16_t TAPS[NTAPS];
static int taps_ready;

static void init_taps(void)
{
    if (taps_ready) return;
    double h[NTAPS], sum = 0;
    for (int i = 0; i < NTAPS; i++) {
        int k = i - NTAPS / 2;
        double x = k ? __builtin_sin(3.14159265358979 * 0.45 * k) / (3.14159265358979 * k) : 0.45;
        double w = 0.54 - 0.46 * __builtin_cos(2 * 3.14159265358979 * i / (NTAPS - 1));   /* Hamming */
        h[i] = x * w; sum += h[i];
    }
    for (int i = 0; i < NTAPS; i++) TAPS[i] = (int16_t)(h[i] / sum * 32767.0 + (h[i] >= 0 ? 0.5 : -0.5));
    taps_ready = 1;
}

static int16_t sat16(int32_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

/* decimate by 2 with FIR history `hist` (NTAPS-1 samples) */
static void decimate(const int16_t *in, size_t n, int16_t *out, int16_t *hist)
{
    int16_t buf[NTAPS - 1 + 640];
    memcpy(buf, hist, sizeof(int16_t) * (NTAPS - 1));
    memcpy(buf + NTAPS - 1, in, sizeof(int16_t) * n);
    for (size_t o = 0; o < n / 2; o++) {
        int32_t acc = 0;
        const int16_t *x = &buf[2 * o + 1];
        for (int k = 0; k < NTAPS; k++) acc += (int32_t)TAPS[k] * x[k];
        out[o] = sat16(acc >> 15);
    }
    memcpy(hist, buf + n, sizeof(int16_t) * (NTAPS - 1));
}

/* interpolate by 2 (zero-stuff + FIR, gain 2) */
static void interpolate(const int16_t *in, size_t n, int16_t *out, int16_t *hist)
{
    int16_t z[NTAPS - 1 + 640];
    memcpy(z, hist, sizeof(int16_t) * (NTAPS - 1));
    for (size_t i = 0; i < n; i++) { z[NTAPS - 1 + 2 * i] = in[i]; z[NTAPS - 1 + 2 * i + 1] = 0; }
    for (size_t o = 0; o < 2 * n; o++) {
        int32_t acc = 0;
        const int16_t *x = &z[o + 1];
        for (int k = 0; k < NTAPS; k++) acc += (int32_t)TAPS[k] * x[k];
        out[o] = sat16(acc >> 14);
    }
    memcpy(hist, z + 2 * n, sizeof(int16_t) * (NTAPS - 1));
}

int vc_bitrate(vc_mode_t mode)
{
    switch (mode) { case VC_MODE_3200: return 3200; case VC_MODE_2400: return 2400;
                    case VC_MODE_1200: return 1200; case VC_MODE_700C: return 700; default: return -1; }
}

uint32_t vc_encoded_size(vc_mode_t mode, uint32_t ms)
{
    struct CODEC2 *c = codec2_create(mode);
    if (!c) return 0;
    uint32_t spf = (uint32_t)codec2_samples_per_frame(c);        /* at 8 kHz */
    uint32_t bpf = (uint32_t)(codec2_bits_per_frame(c) + 7) / 8;
    codec2_destroy(c);
    uint32_t frames = (ms * 8u + spf - 1) / spf;
    return VC_HDR_LEN + frames * bpf;
}

int vc_encode(vc_mode_t mode, const int16_t *pcm, size_t n, uint8_t *out, size_t cap)
{
    init_taps();
    struct CODEC2 *c = codec2_create(mode);
    if (!c) return -1;
    int spf = codec2_samples_per_frame(c), bpf = (codec2_bits_per_frame(c) + 7) / 8;
    size_t frames = (n / 2 + (size_t)spf - 1) / (size_t)spf;
    if (frames > 0xFFFF || cap < VC_HDR_LEN + frames * (size_t)bpf) { codec2_destroy(c); return -2; }
    memcpy(out, "NQV1", 4);
    out[4] = (uint8_t)mode; out[5] = 0; out[6] = (uint8_t)frames; out[7] = (uint8_t)(frames >> 8);
    int16_t hist[NTAPS - 1] = {0}, in16[640], s8[320];
    size_t pos = 0;
    for (size_t f = 0; f < frames; f++) {
        size_t take = (size_t)spf * 2;
        for (size_t i = 0; i < take; i++) in16[i] = pos + i < n ? pcm[pos + i] : 0;
        pos += take;
        decimate(in16, take, s8, hist);
        codec2_encode(c, &out[VC_HDR_LEN + f * (size_t)bpf], s8);
    }
    codec2_destroy(c);
    return (int)(VC_HDR_LEN + frames * (size_t)bpf);
}

int vc_decode(const uint8_t *in, size_t len, int16_t *pcm, size_t cap)
{
    init_taps();
    if (len < VC_HDR_LEN || memcmp(in, "NQV1", 4) != 0) return -1;
    struct CODEC2 *c = codec2_create(in[4]);
    if (!c) return -1;
    int spf = codec2_samples_per_frame(c), bpf = (codec2_bits_per_frame(c) + 7) / 8;
    size_t frames = (size_t)(in[6] | (in[7] << 8));
    if (len < VC_HDR_LEN + frames * (size_t)bpf || cap < frames * (size_t)spf * 2) { codec2_destroy(c); return -2; }
    int16_t hist[NTAPS - 1] = {0}, s8[320];
    for (size_t f = 0; f < frames; f++) {
        codec2_decode(c, s8, &in[VC_HDR_LEN + f * (size_t)bpf]);
        interpolate(s8, (size_t)spf, &pcm[f * (size_t)spf * 2], hist);
    }
    codec2_destroy(c);
    return (int)(frames * (size_t)spf * 2);
}

static long fsize(FILE *f) { fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); return n; }

int vc_encode_file(vc_mode_t mode, const char *pcm_path, const char *c2_path)
{
    FILE *i = fopen(pcm_path, "rb");
    if (!i) return -1;
    long n = fsize(i) / 2;
    int16_t *pcm = malloc((size_t)n * 2 + 2);
    uint32_t cap = vc_encoded_size(mode, (uint32_t)(n / 16)) + 64;
    uint8_t *out = malloc(cap);
    int rc = -1;
    if (pcm && out && fread(pcm, 2, (size_t)n, i) == (size_t)n) {
        int m = vc_encode(mode, pcm, (size_t)n, out, cap);
        FILE *o = m > 0 ? fopen(c2_path, "wb") : NULL;
        if (o) { rc = fwrite(out, 1, (size_t)m, o) == (size_t)m ? m : -1; fclose(o); }
    }
    fclose(i); free(pcm); free(out);
    return rc;
}

int vc_decode_file(const char *c2_path, const char *pcm_path)
{
    FILE *i = fopen(c2_path, "rb");
    if (!i) return -1;
    long n = fsize(i);
    uint8_t *in = malloc((size_t)n);
    int rc = -1;
    if (in && fread(in, 1, (size_t)n, i) == (size_t)n && n >= VC_HDR_LEN) {
        size_t frames = (size_t)(in[6] | (in[7] << 8));
        size_t cap = frames * 640 + 640;
        int16_t *pcm = malloc(cap * 2);
        int m = pcm ? vc_decode(in, (size_t)n, pcm, cap) : -1;
        FILE *o = m > 0 ? fopen(pcm_path, "wb") : NULL;
        if (o) { rc = fwrite(pcm, 2, (size_t)m, o) == (size_t)m ? m : -1; fclose(o); }
        free(pcm);
    }
    fclose(i); free(in);
    return rc;
}
