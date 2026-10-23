/* nomiq_crypto.c — backend-independent part. See include/nomiq_crypto.h */
#include "nomiq_crypto.h"

#include <string.h>

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void nomiq_nonce(uint32_t src, uint32_t ctr, uint8_t type, uint16_t msg_id, uint16_t frag_ix, uint8_t nonce[13])
{
    put32(&nonce[0], src);
    put32(&nonce[4], ctr);
    nonce[8] = type;
    put16(&nonce[9], msg_id);
    put16(&nonce[11], frag_ix);
}

int nomiq_frame_seal(const uint8_t key[16], uint32_t ctr, nomiq_hdr_t *hdr, const uint8_t *payload,
                     uint8_t *buf, size_t buf_len)
{
    if (!key || !hdr || !buf) return NOMIQ_E_ARG;
    size_t total = NOMIQ_HDR_LEN + (size_t)hdr->len + NOMIQ_SEC_LEN;
    if (hdr->len > NOMIQ_MAX_PAYLOAD) return NOMIQ_E_LEN;
    if (buf_len < total) return NOMIQ_E_SHORT;

    /* plaintext encode gives us the packed header; then switch to encrypted layout */
    int n = nomiq_frame_encode(hdr, payload, buf, buf_len);
    if (n < 0) return n;
    hdr->flags |= NOMIQ_F_ENCRYPTED;
    buf[2] = hdr->flags;

    uint8_t pt[NOMIQ_MAX_PAYLOAD];
    if (hdr->len) memcpy(pt, &buf[NOMIQ_HDR_LEN], hdr->len);
    put32(&buf[NOMIQ_HDR_LEN], ctr);
    uint8_t *ct = &buf[NOMIQ_HDR_LEN + NOMIQ_CTR_LEN];
    uint8_t *tag = ct + hdr->len;

    uint8_t nonce[NOMIQ_NONCE_LEN];
    nomiq_nonce(hdr->src, ctr, hdr->type, hdr->msg_id, hdr->frag_ix, nonce);
    if (nomiq_ccm_encrypt(key, nonce, buf, 18, pt, hdr->len, ct, tag, NOMIQ_MIC_LEN) != 0) return NOMIQ_E_ARG;

    uint16_t crc = nomiq_crc16(buf, 18, 0xFFFFu);
    crc = nomiq_crc16(&buf[NOMIQ_HDR_LEN], total - NOMIQ_HDR_LEN, crc);
    hdr->crc16 = crc;
    put16(&buf[18], crc);
    memset(pt, 0, sizeof pt);
    return (int)total;
}

int nomiq_frame_open(const uint8_t key[16], uint8_t *buf, size_t len, nomiq_hdr_t *hdr,
                     const uint8_t **payload, uint32_t *ctr)
{
    const uint8_t *body;
    int rc = nomiq_frame_decode(buf, len, hdr, &body);
    if (rc != NOMIQ_OK) return rc;
    if (!(hdr->flags & NOMIQ_F_ENCRYPTED)) return NOMIQ_E_AUTH;

    uint32_t c = get32(&buf[NOMIQ_HDR_LEN]);
    uint8_t *ct = &buf[NOMIQ_HDR_LEN + NOMIQ_CTR_LEN];
    const uint8_t *tag = ct + hdr->len;
    uint8_t nonce[NOMIQ_NONCE_LEN];
    nomiq_nonce(hdr->src, c, hdr->type, hdr->msg_id, hdr->frag_ix, nonce);

    uint8_t pt[NOMIQ_MAX_PAYLOAD];
    if (nomiq_ccm_decrypt(key, nonce, buf, 18, ct, hdr->len, pt, tag, NOMIQ_MIC_LEN) != 0) return NOMIQ_E_AUTH;
    memcpy(ct, pt, hdr->len);
    memset(pt, 0, sizeof pt);
    if (payload) *payload = hdr->len ? ct : NULL;
    if (ctr) *ctr = c;
    return NOMIQ_OK;
}

bool nomiq_replay_accept(nomiq_replay_t *w, uint32_t ctr)
{
    if (!w->init) { w->init = true; w->top = ctr; w->mask = 1; return true; }
    if ((int32_t)(ctr - w->top) > 0) {
        uint32_t shift = ctr - w->top;
        w->mask = shift >= 64 ? 0 : (w->mask << shift);
        w->mask |= 1;
        w->top = ctr;
        return true;
    }
    uint32_t back = w->top - ctr;
    if (back >= 64) return false;
    if (w->mask & ((uint64_t)1 << back)) return false;
    w->mask |= (uint64_t)1 << back;
    return true;
}

int nomiq_pair_derive(const uint8_t shared[32], uint32_t id_a, uint32_t id_b, uint8_t key[16], uint32_t *code6)
{
    static const uint8_t salt[] = "nomiq v1";
    uint32_t lo = id_a < id_b ? id_a : id_b, hi = id_a < id_b ? id_b : id_a;
    uint8_t info[4 + 8];
    put32(&info[4], lo);
    put32(&info[8], hi);

    memcpy(info, "key ", 4);
    if (nomiq_hkdf_sha256(salt, sizeof salt - 1, shared, 32, info, sizeof info, key, 16) != 0) return -1;
    if (code6) {
        uint8_t c[4];
        memcpy(info, "code", 4);
        if (nomiq_hkdf_sha256(salt, sizeof salt - 1, shared, 32, info, sizeof info, c, 4) != 0) return -1;
        *code6 = get32(c) % 1000000u;
    }
    return 0;
}
