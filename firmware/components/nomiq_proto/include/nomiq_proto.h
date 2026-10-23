/*
 * nomiq_proto — Nomiq air protocol core (v1).
 *
 * Pure C99, no ESP-IDF dependencies, so it is unit-tested on the host.
 * Normative spec: docs/specs/communication-protocol.md
 */
#ifndef NOMIQ_PROTO_H
#define NOMIQ_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NOMIQ_PROTO_VERSION     1u
#define NOMIQ_LORA_MAX_FRAME    255u
#define NOMIQ_HDR_LEN           20u
#define NOMIQ_CTR_LEN           4u   /* per-sender frame counter (nonce + replay protection) */
#define NOMIQ_MIC_LEN           8u   /* AES-128-CCM tag */
#define NOMIQ_SEC_LEN           (NOMIQ_CTR_LEN + NOMIQ_MIC_LEN)
#define NOMIQ_MAX_PAYLOAD       (NOMIQ_LORA_MAX_FRAME - NOMIQ_HDR_LEN - NOMIQ_SEC_LEN) /* 223 */
#define NOMIQ_BROADCAST         0xFFFFFFFFu

#define NOMIQ_MAX_FRAGS         512u   /* 512 * 223 B = 114 kB max object */
#define NOMIQ_WINDOW            16u
#define NOMIQ_DEDUP_ENTRIES     64u

typedef enum {
    NOMIQ_T_TEXT   = 0x01,
    NOMIQ_T_OFFER  = 0x02,
    NOMIQ_T_ACCEPT = 0x03,
    NOMIQ_T_DATA   = 0x04,
    NOMIQ_T_SACK   = 0x05,
    NOMIQ_T_DONE   = 0x06,
    NOMIQ_T_CANCEL = 0x07,
    NOMIQ_T_REJECT = 0x08,
    NOMIQ_T_BEACON = 0x09,
    NOMIQ_T_PAIR   = 0x0A,
    NOMIQ_T_ACK    = 0x0B,
} nomiq_type_t;

/* flags */
#define NOMIQ_F_PRIO_MASK   0x03u
#define NOMIQ_F_ACK_REQ     0x04u
#define NOMIQ_F_RETX        0x08u
#define NOMIQ_F_ENCRYPTED   0x10u

typedef enum {
    NOMIQ_PRIO_CONTROL = 0,
    NOMIQ_PRIO_TEXT    = 1,
    NOMIQ_PRIO_VOICE   = 2,
    NOMIQ_PRIO_PHOTO   = 3,
} nomiq_prio_t;

typedef struct {
    uint8_t  version;
    uint8_t  type;
    uint8_t  flags;
    uint8_t  len;        /* payload length */
    uint32_t src;
    uint32_t dst;
    uint16_t msg_id;
    uint16_t frag_ix;
    uint16_t frag_n;
    uint16_t crc16;      /* filled by nomiq_frame_encode */
} nomiq_hdr_t;

typedef enum {
    NOMIQ_OK = 0,
    NOMIQ_E_ARG = -1,
    NOMIQ_E_SHORT = -2,
    NOMIQ_E_VERSION = -3,
    NOMIQ_E_LEN = -4,
    NOMIQ_E_CRC = -5,
    NOMIQ_E_RANGE = -6,
} nomiq_err_t;

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF). */
uint16_t nomiq_crc16(const uint8_t *data, size_t len, uint16_t crc);

/*
 * Encode a plaintext frame: header + payload. Computes and stores hdr->crc16.
 * (Encrypted frames are produced by nomiq_frame_seal() in nomiq_crypto.h.)
 * Returns frame length or negative nomiq_err_t.
 *
 * CRC-16 always covers header bytes 0..17 and every byte after the header
 * (payload, or counter+ciphertext+MIC for encrypted frames).
 */
int nomiq_frame_encode(nomiq_hdr_t *hdr, const uint8_t *payload,
                       uint8_t *buf, size_t buf_len);

/*
 * Decode a frame and check its CRC. For plaintext frames *payload points at the payload.
 * For frames with NOMIQ_F_ENCRYPTED, *payload points at counter+ciphertext+MIC and the
 * caller must use nomiq_frame_open() instead. Returns NOMIQ_OK or negative error.
 */
int nomiq_frame_decode(const uint8_t *buf, size_t len,
                       nomiq_hdr_t *hdr, const uint8_t **payload);

/* Number of fragments needed for an object of `size` bytes (>=1). */
uint16_t nomiq_frag_count(uint32_t size);

/* Byte offset and length of fragment `ix` of an object of `size` bytes. */
int nomiq_frag_span(uint32_t size, uint16_t ix, uint32_t *off, uint16_t *len);

/* ---------- Reassembly (receiver) ---------- */

typedef struct {
    uint32_t src;
    uint16_t msg_id;
    uint16_t frag_n;
    uint32_t size;
    uint16_t received;
    uint8_t  bitmap[NOMIQ_MAX_FRAGS / 8];
} nomiq_rx_obj_t;

int  nomiq_rx_init(nomiq_rx_obj_t *o, uint32_t src, uint16_t msg_id, uint32_t size);
/* Marks fragment received; returns 1 if new, 0 if duplicate, negative on error. */
int  nomiq_rx_mark(nomiq_rx_obj_t *o, uint16_t ix);
bool nomiq_rx_has(const nomiq_rx_obj_t *o, uint16_t ix);
bool nomiq_rx_complete(const nomiq_rx_obj_t *o);
/* First missing fragment index, or frag_n if complete. */
uint16_t nomiq_rx_first_missing(const nomiq_rx_obj_t *o);
/* 64-bit SACK bitmap: bit i set => fragment (base + i) held. */
uint64_t nomiq_rx_sack(const nomiq_rx_obj_t *o, uint16_t base);

/* ---------- Duplicate filter ---------- */

typedef struct {
    uint32_t src;
    uint16_t msg_id;
    uint16_t frag_ix;
    uint8_t  type;
    uint8_t  valid;
} nomiq_dedup_entry_t;

typedef struct {
    nomiq_dedup_entry_t e[NOMIQ_DEDUP_ENTRIES];
    uint8_t next;
} nomiq_dedup_t;

void nomiq_dedup_init(nomiq_dedup_t *d);
/* Returns true if this (src,msg_id,frag_ix,type) was seen; otherwise records it. */
bool nomiq_dedup_seen(nomiq_dedup_t *d, const nomiq_hdr_t *h);

/* ---------- Retry backoff ---------- */

/* Delay before attempt `n` (0-based) in ms: (2^n) * (toa_ms + 200) + jitter. */
uint32_t nomiq_backoff_ms(uint8_t attempt, uint32_t toa_ms, uint32_t jitter_ms);

#ifdef __cplusplus
}
#endif
#endif /* NOMIQ_PROTO_H */
