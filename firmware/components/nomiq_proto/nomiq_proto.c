/* nomiq_proto.c — see include/nomiq_proto.h and docs/specs/communication-protocol.md */
#include "nomiq_proto.h"

#include <string.h>

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint16_t nomiq_crc16(const uint8_t *data, size_t len, uint16_t crc)
{
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static void hdr_pack(const nomiq_hdr_t *h, uint8_t *b)
{
    b[0] = h->version;
    b[1] = h->type;
    b[2] = h->flags;
    b[3] = h->len;
    put32(&b[4], h->src);
    put32(&b[8], h->dst);
    put16(&b[12], h->msg_id);
    put16(&b[14], h->frag_ix);
    put16(&b[16], h->frag_n);
    put16(&b[18], h->crc16);
}

int nomiq_frame_encode(nomiq_hdr_t *hdr, const uint8_t *payload, uint8_t *buf, size_t buf_len)
{
    if (!hdr || !buf || (hdr->len && !payload)) return NOMIQ_E_ARG;
    if (hdr->len > NOMIQ_MAX_PAYLOAD) return NOMIQ_E_LEN;
    size_t total = NOMIQ_HDR_LEN + hdr->len;
    if (buf_len < total) return NOMIQ_E_SHORT;

    hdr->version = NOMIQ_PROTO_VERSION;
    hdr->flags &= (uint8_t)~NOMIQ_F_ENCRYPTED;   /* plaintext encoder */
    hdr->crc16 = 0;
    hdr_pack(hdr, buf);
    if (hdr->len) memcpy(&buf[NOMIQ_HDR_LEN], payload, hdr->len);

    uint16_t crc = nomiq_crc16(buf, 18, 0xFFFFu);
    crc = nomiq_crc16(&buf[NOMIQ_HDR_LEN], hdr->len, crc);
    hdr->crc16 = crc;
    put16(&buf[18], crc);
    return (int)total;
}

int nomiq_frame_decode(const uint8_t *buf, size_t len, nomiq_hdr_t *hdr, const uint8_t **payload)
{
    if (!buf || !hdr) return NOMIQ_E_ARG;
    if (len < NOMIQ_HDR_LEN) return NOMIQ_E_SHORT;
    if (buf[0] != NOMIQ_PROTO_VERSION) return NOMIQ_E_VERSION;

    hdr->version = buf[0];
    hdr->type    = buf[1];
    hdr->flags   = buf[2];
    hdr->len     = buf[3];
    hdr->src     = get32(&buf[4]);
    hdr->dst     = get32(&buf[8]);
    hdr->msg_id  = get16(&buf[12]);
    hdr->frag_ix = get16(&buf[14]);
    hdr->frag_n  = get16(&buf[16]);
    hdr->crc16   = get16(&buf[18]);

    size_t body = (size_t)hdr->len + ((hdr->flags & NOMIQ_F_ENCRYPTED) ? NOMIQ_SEC_LEN : 0u);
    if (hdr->len > NOMIQ_MAX_PAYLOAD || NOMIQ_HDR_LEN + body != len) return NOMIQ_E_LEN;
    if (hdr->frag_n == 0 || hdr->frag_ix >= hdr->frag_n) {
        /* SACK uses frag_ix as window base, still must be < frag_n */
        return NOMIQ_E_RANGE;
    }

    uint16_t crc = nomiq_crc16(buf, 18, 0xFFFFu);
    crc = nomiq_crc16(&buf[NOMIQ_HDR_LEN], body, crc);
    if (crc != hdr->crc16) return NOMIQ_E_CRC;

    if (payload) *payload = body ? &buf[NOMIQ_HDR_LEN] : NULL;
    return NOMIQ_OK;
}

uint16_t nomiq_frag_count(uint32_t size)
{
    if (size == 0) return 1;
    return (uint16_t)((size + NOMIQ_MAX_PAYLOAD - 1) / NOMIQ_MAX_PAYLOAD);
}

int nomiq_frag_span(uint32_t size, uint16_t ix, uint32_t *off, uint16_t *len)
{
    uint16_t n = nomiq_frag_count(size);
    if (!off || !len) return NOMIQ_E_ARG;
    if (ix >= n) return NOMIQ_E_RANGE;
    *off = (uint32_t)ix * NOMIQ_MAX_PAYLOAD;
    uint32_t rem = size - *off;
    *len = (uint16_t)(rem > NOMIQ_MAX_PAYLOAD ? NOMIQ_MAX_PAYLOAD : rem);
    return NOMIQ_OK;
}

/* ---------- Reassembly ---------- */

int nomiq_rx_init(nomiq_rx_obj_t *o, uint32_t src, uint16_t msg_id, uint32_t size)
{
    if (!o) return NOMIQ_E_ARG;
    uint16_t n = nomiq_frag_count(size);
    if (n > NOMIQ_MAX_FRAGS) return NOMIQ_E_RANGE;
    memset(o, 0, sizeof(*o));
    o->src = src;
    o->msg_id = msg_id;
    o->size = size;
    o->frag_n = n;
    return NOMIQ_OK;
}

bool nomiq_rx_has(const nomiq_rx_obj_t *o, uint16_t ix)
{
    return o && ix < o->frag_n && (o->bitmap[ix >> 3] & (1u << (ix & 7)));
}

int nomiq_rx_mark(nomiq_rx_obj_t *o, uint16_t ix)
{
    if (!o) return NOMIQ_E_ARG;
    if (ix >= o->frag_n) return NOMIQ_E_RANGE;
    if (nomiq_rx_has(o, ix)) return 0;
    o->bitmap[ix >> 3] |= (uint8_t)(1u << (ix & 7));
    o->received++;
    return 1;
}

bool nomiq_rx_complete(const nomiq_rx_obj_t *o) { return o && o->received == o->frag_n; }

uint16_t nomiq_rx_first_missing(const nomiq_rx_obj_t *o)
{
    for (uint16_t i = 0; i < o->frag_n; i++) {
        if (!nomiq_rx_has(o, i)) return i;
    }
    return o->frag_n;
}

uint64_t nomiq_rx_sack(const nomiq_rx_obj_t *o, uint16_t base)
{
    uint64_t bm = 0;
    for (uint16_t i = 0; i < 64; i++) {
        uint32_t ix = (uint32_t)base + i;
        if (ix >= o->frag_n) break;
        if (nomiq_rx_has(o, (uint16_t)ix)) bm |= (uint64_t)1 << i;
    }
    return bm;
}

/* ---------- Dedup ---------- */

void nomiq_dedup_init(nomiq_dedup_t *d) { memset(d, 0, sizeof(*d)); }

bool nomiq_dedup_seen(nomiq_dedup_t *d, const nomiq_hdr_t *h)
{
    for (unsigned i = 0; i < NOMIQ_DEDUP_ENTRIES; i++) {
        const nomiq_dedup_entry_t *e = &d->e[i];
        if (e->valid && e->src == h->src && e->msg_id == h->msg_id &&
            e->frag_ix == h->frag_ix && e->type == h->type) {
            return true;
        }
    }
    nomiq_dedup_entry_t *e = &d->e[d->next];
    e->src = h->src;
    e->msg_id = h->msg_id;
    e->frag_ix = h->frag_ix;
    e->type = h->type;
    e->valid = 1;
    d->next = (uint8_t)((d->next + 1) % NOMIQ_DEDUP_ENTRIES);
    return false;
}

/* ---------- Backoff ---------- */

uint32_t nomiq_backoff_ms(uint8_t attempt, uint32_t toa_ms, uint32_t jitter_ms)
{
    if (attempt > 6) attempt = 6;
    return ((uint32_t)1u << attempt) * (toa_ms + 200u) + jitter_ms;
}
