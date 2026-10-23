#include "stt_proto.h"

#include <string.h>

enum { S_SOF, S_HDR, S_PAYLOAD, S_CRC0, S_CRC1 };

uint16_t stt_crc16(const uint8_t *d, size_t n, uint16_t crc)
{
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)d[i] << 8;
        for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

int stt_encode(uint8_t type, const uint8_t *payload, uint32_t len, uint8_t *out, size_t cap)
{
    if (len > STT_MAX_PAYLOAD || cap < (size_t)len + STT_OVERHEAD) return -1;
    out[0] = STT_SOF;
    out[1] = type;
    out[2] = (uint8_t)len; out[3] = (uint8_t)(len >> 8); out[4] = (uint8_t)(len >> 16); out[5] = (uint8_t)(len >> 24);
    if (len) memcpy(&out[6], payload, len);
    uint16_t c = stt_crc16(&out[1], 5u + len, 0xFFFF);
    out[6 + len] = (uint8_t)c;
    out[7 + len] = (uint8_t)(c >> 8);
    return (int)(len + STT_OVERHEAD);
}

void stt_parser_init(stt_parser_t *p) { memset(p, 0, sizeof(*p)); }

static uint32_t rd32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

/* 1 = frame ready, 0 = need more bytes, -1 = invalid candidate */
static int check(stt_parser_t *p)
{
    if (p->n < 6) return 0;
    uint32_t len = rd32(&p->raw[2]);
    if (len > STT_MAX_PAYLOAD) return -1;
    if (p->n < len + STT_OVERHEAD) return 0;
    uint16_t c = stt_crc16(&p->raw[1], 5u + len, 0xFFFF);
    uint16_t r = (uint16_t)(p->raw[6 + len] | (p->raw[7 + len] << 8));
    if (c != r) { p->crc_errors++; return -1; }
    p->type = p->raw[1];
    p->len = len;
    p->payload = &p->raw[6];
    p->consume = len + STT_OVERHEAD;
    return 1;
}

static void shift(stt_parser_t *p, uint32_t k)
{
    if (k >= p->n) { p->n = 0; return; }
    memmove(p->raw, &p->raw[k], p->n - k);
    p->n -= k;
}

static void resync(stt_parser_t *p)
{
    uint32_t k = 1;
    while (k < p->n && p->raw[k] != STT_SOF) k++;
    shift(p, k);
}

bool stt_parser_feed(stt_parser_t *p, uint8_t b)
{
    if (p->consume) {
        shift(p, p->consume);
        p->consume = 0;
        if (p->n && p->raw[0] != STT_SOF) resync(p);
    }
    if (p->n == 0 && b != STT_SOF) return false;
    if (p->n >= sizeof p->raw) resync(p);
    p->raw[p->n++] = b;
    for (;;) {
        int r = check(p);
        if (r == 1) return true;
        if (r == 0) return false;
        resync(p);
        if (p->n == 0) return false;
    }
}

int stt_final_decode(const uint8_t *p, uint32_t len, char *text, size_t cap, float *confidence)
{
    if (len < 2 || cap == 0) return -1;
    uint16_t c = (uint16_t)(p[0] | (p[1] << 8));
    size_t n = len - 2;
    if (n >= cap) n = cap - 1;          /* truncate on a UTF-8 boundary */
    while (n > 0 && (p[2 + n] & 0xC0) == 0x80 && n < len - 2) n--;
    memcpy(text, &p[2], n);
    text[n] = 0;
    if (confidence) *confidence = c / 1000.0f;
    return (int)n;
}
