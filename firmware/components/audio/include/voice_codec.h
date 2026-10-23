/*
 * voice_codec — Nomiq voice-note format on top of Codec2 (ADR-0004).
 *
 *   capture 16 kHz s16 mono --(anti-alias FIR, /2)--> 8 kHz --Codec2--> .c2 file
 *   .c2 file --Codec2--> 8 kHz --(x2, FIR)--> 16 kHz s16 for the I2S amplifier
 *
 * .c2 file: "NQV1" | mode u8 | reserved u8 | frames u16 LE | frames × bytes_per_frame
 * Pure C (+ vendored codec2), host-tested.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { VC_MODE_3200 = 0, VC_MODE_2400 = 1, VC_MODE_1200 = 5, VC_MODE_700C = 8 } vc_mode_t;  /* = CODEC2_MODE_* */

#define VC_HDR_LEN 8

/* Bytes needed for `ms` of audio in `mode` (incl. header). */
uint32_t vc_encoded_size(vc_mode_t mode, uint32_t ms);
int      vc_bitrate(vc_mode_t mode);

/* Encode 16 kHz PCM to an in-memory .c2 image. Returns bytes written or < 0. */
int vc_encode(vc_mode_t mode, const int16_t *pcm16k, size_t n, uint8_t *out, size_t cap);
/* Decode a .c2 image to 16 kHz PCM. Returns samples written or < 0. */
int vc_decode(const uint8_t *in, size_t len, int16_t *pcm16k, size_t cap);

/* File helpers (stream through small buffers; suitable for PSRAM-constrained devices). */
int vc_encode_file(vc_mode_t mode, const char *pcm16k_path, const char *c2_path);
int vc_decode_file(const char *c2_path, const char *pcm16k_path);

#ifdef __cplusplus
}
#endif
