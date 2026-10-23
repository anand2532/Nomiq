/* Host unit tests for nomiq_proto. Build: make -C firmware/components/nomiq_proto/test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nomiq_proto.h"

static int fails, passes;
#define CHECK(c) do { if (c) passes++; else { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static void test_crc_known_vector(void)
{
    /* CRC-16/CCITT-FALSE("123456789") = 0x29B1 */
    CHECK(nomiq_crc16((const uint8_t *)"123456789", 9, 0xFFFF) == 0x29B1);
}

static void test_roundtrip(void)
{
    uint8_t buf[NOMIQ_LORA_MAX_FRAME];
    const char *msg = "hello from nomiq";
    nomiq_hdr_t h = { .type = NOMIQ_T_TEXT, .flags = NOMIQ_F_ACK_REQ | NOMIQ_PRIO_TEXT,
                      .len = (uint8_t)strlen(msg), .src = 0xA1B2C3D4, .dst = 0x01020304,
                      .msg_id = 42, .frag_ix = 0, .frag_n = 1 };
    int n = nomiq_frame_encode(&h, (const uint8_t *)msg, buf, sizeof buf);
    CHECK(n == (int)(NOMIQ_HDR_LEN + strlen(msg)));
    CHECK(buf[4] == 0xD4 && buf[7] == 0xA1); /* little-endian src */

    nomiq_hdr_t d; const uint8_t *pl;
    CHECK(nomiq_frame_decode(buf, (size_t)n, &d, &pl) == NOMIQ_OK);
    CHECK(d.src == h.src && d.dst == h.dst && d.msg_id == 42 && d.type == NOMIQ_T_TEXT);
    CHECK(memcmp(pl, msg, strlen(msg)) == 0);

    buf[NOMIQ_HDR_LEN + 3] ^= 0x01; /* corrupt payload */
    CHECK(nomiq_frame_decode(buf, (size_t)n, &d, &pl) == NOMIQ_E_CRC);
    buf[NOMIQ_HDR_LEN + 3] ^= 0x01;
    buf[0] = 9; /* bad version */
    CHECK(nomiq_frame_decode(buf, (size_t)n, &d, &pl) == NOMIQ_E_VERSION);
    buf[0] = 1;
    CHECK(nomiq_frame_decode(buf, (size_t)n - 1, &d, &pl) == NOMIQ_E_LEN);
    CHECK(nomiq_frame_decode(buf, 5, &d, &pl) == NOMIQ_E_SHORT);
}

static void test_limits(void)
{
    uint8_t buf[NOMIQ_LORA_MAX_FRAME], pl[NOMIQ_MAX_PAYLOAD + 1] = {0};
    CHECK(NOMIQ_MAX_PAYLOAD == 227);
    nomiq_hdr_t h = { .type = NOMIQ_T_DATA, .len = NOMIQ_MAX_PAYLOAD, .frag_n = 1 };
    int n = nomiq_frame_encode(&h, pl, buf, sizeof buf);
    CHECK(n == NOMIQ_HDR_LEN + NOMIQ_MAX_PAYLOAD);
    CHECK(n + NOMIQ_MIC_LEN == NOMIQ_LORA_MAX_FRAME);
    h.len = NOMIQ_MAX_PAYLOAD + 1;
    CHECK(nomiq_frame_encode(&h, pl, buf, sizeof buf) == NOMIQ_E_LEN);
}

static void test_fragmentation(void)
{
    CHECK(nomiq_frag_count(0) == 1);
    CHECK(nomiq_frag_count(227) == 1);
    CHECK(nomiq_frag_count(228) == 2);
    CHECK(nomiq_frag_count(12000) == 53);
    uint32_t off; uint16_t len;
    CHECK(nomiq_frag_span(12000, 52, &off, &len) == NOMIQ_OK);
    CHECK(off == 52u * 227u && len == 12000 - 52 * 227);
    CHECK(nomiq_frag_span(12000, 53, &off, &len) == NOMIQ_E_RANGE);
}

/* Simulate a lossy transfer: object reassembled with selective retransmission. */
static void test_lossy_transfer(void)
{
    const uint32_t size = 20000;
    uint8_t *src = malloc(size), *dst = calloc(1, size);
    for (uint32_t i = 0; i < size; i++) src[i] = (uint8_t)(i * 31 + 7);

    nomiq_rx_obj_t rx; nomiq_dedup_t dd;
    CHECK(nomiq_rx_init(&rx, 0x11, 7, size) == NOMIQ_OK);
    nomiq_dedup_init(&dd);
    srand(1234);

    int rounds = 0, sent = 0;
    while (!nomiq_rx_complete(&rx) && rounds < 100) {
        uint16_t base = nomiq_rx_first_missing(&rx);
        uint64_t sack = nomiq_rx_sack(&rx, base);
        unsigned in_window = 0;
        for (uint16_t i = 0; i < 64 && in_window < NOMIQ_WINDOW; i++) {
            uint16_t ix = (uint16_t)(base + i);
            if (ix >= rx.frag_n) break;
            if (sack & ((uint64_t)1 << i)) continue; /* receiver already has it */
            in_window++;
            uint32_t off; uint16_t len;
            nomiq_frag_span(size, ix, &off, &len);
            uint8_t frame[NOMIQ_LORA_MAX_FRAME];
            nomiq_hdr_t h = { .type = NOMIQ_T_DATA, .flags = NOMIQ_PRIO_PHOTO, .len = (uint8_t)len,
                              .src = 0x11, .dst = 0x22, .msg_id = 7, .frag_ix = ix, .frag_n = rx.frag_n };
            int n = nomiq_frame_encode(&h, &src[off], frame, sizeof frame);
            sent++;
            if (rand() % 100 < 30) continue;            /* 30% loss */
            if (rand() % 100 < 5) frame[25] ^= 0x40;    /* 5% corruption */
            nomiq_hdr_t d; const uint8_t *pl;
            if (nomiq_frame_decode(frame, (size_t)n, &d, &pl) != NOMIQ_OK) continue;
            if (nomiq_dedup_seen(&dd, &d)) continue;
            uint32_t o2; uint16_t l2;
            nomiq_frag_span(size, d.frag_ix, &o2, &l2);
            memcpy(&dst[o2], pl, l2);
            nomiq_rx_mark(&rx, d.frag_ix);
        }
        rounds++;
    }
    CHECK(nomiq_rx_complete(&rx));
    CHECK(memcmp(src, dst, size) == 0);
    printf("  lossy transfer: %u frags, %d frames sent, %d windows\n", rx.frag_n, sent, rounds);
    free(src); free(dst);
}

static void test_dedup(void)
{
    nomiq_dedup_t d; nomiq_dedup_init(&d);
    nomiq_hdr_t h = { .type = NOMIQ_T_DATA, .src = 5, .msg_id = 1, .frag_ix = 3 };
    CHECK(!nomiq_dedup_seen(&d, &h));
    CHECK(nomiq_dedup_seen(&d, &h));
    h.type = NOMIQ_T_SACK;
    CHECK(!nomiq_dedup_seen(&d, &h));
    /* evict after NOMIQ_DEDUP_ENTRIES new entries */
    nomiq_hdr_t first = { .type = NOMIQ_T_DATA, .src = 5, .msg_id = 1, .frag_ix = 3 };
    for (uint16_t i = 0; i < NOMIQ_DEDUP_ENTRIES; i++) {
        nomiq_hdr_t x = { .type = NOMIQ_T_DATA, .src = 9, .msg_id = 2, .frag_ix = i };
        nomiq_dedup_seen(&d, &x);
    }
    CHECK(!nomiq_dedup_seen(&d, &first));
}

static void test_rx_resume_and_range(void)
{
    nomiq_rx_obj_t o;
    CHECK(nomiq_rx_init(&o, 1, 1, NOMIQ_MAX_FRAGS * NOMIQ_MAX_PAYLOAD + 1) == NOMIQ_E_RANGE);
    CHECK(nomiq_rx_init(&o, 1, 1, 1000) == NOMIQ_OK); /* 5 frags */
    CHECK(nomiq_rx_mark(&o, 0) == 1);
    CHECK(nomiq_rx_mark(&o, 0) == 0);
    CHECK(nomiq_rx_mark(&o, 3) == 1);
    CHECK(nomiq_rx_mark(&o, 5) == NOMIQ_E_RANGE);
    CHECK(nomiq_rx_first_missing(&o) == 1);
    CHECK(nomiq_rx_sack(&o, 0) == 0x9u);
    CHECK(nomiq_backoff_ms(0, 400, 0) == 600 && nomiq_backoff_ms(3, 400, 10) == 4810);
}

int main(void)
{
    test_crc_known_vector();
    test_roundtrip();
    test_limits();
    test_fragmentation();
    test_lossy_transfer();
    test_dedup();
    test_rx_resume_and_range();
    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
