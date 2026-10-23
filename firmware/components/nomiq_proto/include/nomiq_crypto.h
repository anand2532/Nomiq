/*
 * nomiq_crypto — link security for the Nomiq air protocol.
 *
 *   Frame (encrypted):  header(20, flags.ENCRYPTED) | ctr(4) | ciphertext(len) | MIC(8)
 *   Cipher:             AES-128-CCM, 8-byte tag, AAD = header bytes 0..17
 *   Nonce (13 B):       src(4) | ctr(4) | type(1) | msg_id(2) | frag_ix(2)   (little-endian)
 *   ctr:                per-sender 32-bit frame counter, never reused under one key
 *                       (persisted in NVS in blocks; see docs/specs/communication-protocol.md §4)
 *   Pairing:            X25519 ECDH -> HKDF-SHA256 -> 16-byte link key + 6-digit confirmation code
 *
 * Backends: OpenSSL (host tests) or mbedTLS (ESP-IDF), selected at build time.
 */
#ifndef NOMIQ_CRYPTO_H
#define NOMIQ_CRYPTO_H

#include "nomiq_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NOMIQ_KEY_LEN    16u
#define NOMIQ_NONCE_LEN  13u
#define NOMIQ_X25519_LEN 32u

/* ---- primitives (backend) ---- */
int nomiq_ccm_encrypt(const uint8_t key[16], const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                      const uint8_t *in, size_t len, uint8_t *out, uint8_t *tag, size_t tag_len);
/* Returns 0 if the tag verifies (out holds plaintext), -1 otherwise (out is zeroed). */
int nomiq_ccm_decrypt(const uint8_t key[16], const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                      const uint8_t *in, size_t len, uint8_t *out, const uint8_t *tag, size_t tag_len);
int nomiq_hkdf_sha256(const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len,
                      const uint8_t *info, size_t info_len, uint8_t *out, size_t out_len);
/* Clamps priv in place, writes the public key. */
int nomiq_x25519_public(uint8_t priv[32], uint8_t pub[32]);
int nomiq_x25519_shared(const uint8_t priv[32], const uint8_t peer_pub[32], uint8_t shared[32]);
int nomiq_random(uint8_t *buf, size_t len);

/* Incremental SHA-256 (object integrity: first 8 bytes go into OFFER). */
typedef struct { uint64_t opaque[28]; } nomiq_sha256_t;   /* 224 B, 8-byte aligned */
int nomiq_sha256_init(nomiq_sha256_t *c);
int nomiq_sha256_update(nomiq_sha256_t *c, const uint8_t *d, size_t n);
int nomiq_sha256_final(nomiq_sha256_t *c, uint8_t out[32]);

/* ---- protocol helpers ---- */
void nomiq_nonce(uint32_t src, uint32_t ctr, uint8_t type, uint16_t msg_id, uint16_t frag_ix,
                 uint8_t nonce[13]);

/* Build an encrypted frame. hdr->flags gets NOMIQ_F_ENCRYPTED. Returns frame length or < 0. */
int nomiq_frame_seal(const uint8_t key[16], uint32_t ctr, nomiq_hdr_t *hdr, const uint8_t *payload,
                     uint8_t *buf, size_t buf_len);

/* Verify CRC + MIC and decrypt in place. On success *payload points at plaintext inside buf
 * and *ctr receives the sender's frame counter. Returns NOMIQ_OK, NOMIQ_E_* or NOMIQ_E_AUTH. */
#define NOMIQ_E_AUTH (-7)
#define NOMIQ_E_REPLAY (-8)
int nomiq_frame_open(const uint8_t key[16], uint8_t *buf, size_t len, nomiq_hdr_t *hdr,
                     const uint8_t **payload, uint32_t *ctr);

/* Sliding replay window (64 frames) per peer. */
typedef struct { uint32_t top; uint64_t mask; bool init; } nomiq_replay_t;
/* Returns true and records ctr if it is fresh; false for replays / too old. */
bool nomiq_replay_accept(nomiq_replay_t *w, uint32_t ctr);

/* Pairing: derive the link key and a 6-digit code both users compare on screen. */
int nomiq_pair_derive(const uint8_t shared[32], uint32_t id_a, uint32_t id_b,
                      uint8_t key[16], uint32_t *code6);

#ifdef __cplusplus
}
#endif
#endif /* NOMIQ_CRYPTO_H */
