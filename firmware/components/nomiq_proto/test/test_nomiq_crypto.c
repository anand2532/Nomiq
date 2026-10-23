/* Crypto tests: published vectors (RFC 3610, RFC 5869, RFC 7748) + frame seal/open + replay. */
#include <stdio.h>
#include <string.h>

#include "nomiq_crypto.h"

static int fails, passes;
#define CHECK(c) do { if (c) passes++; else { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static void hex(const char *s, uint8_t *out)
{
    for (size_t i = 0; s[2 * i] && s[2 * i + 1]; i++) {
        unsigned v; sscanf(&s[2 * i], "%2x", &v); out[i] = (uint8_t)v;
    }
}

static void test_ccm_rfc3610_vector1(void)
{
    uint8_t key[16], nonce[13], aad[8], pt[23], ct[23], tag[8], exp_ct[23], exp_tag[8], back[23];
    hex("c0c1c2c3c4c5c6c7c8c9cacbcccdcecf", key);
    hex("00000003020100a0a1a2a3a4a5", nonce);
    hex("0001020304050607", aad);
    hex("08090a0b0c0d0e0f101112131415161718191a1b1c1d1e", pt);
    hex("588c979a61c663d2f066d0c2c0f989806d5f6b61dac384", exp_ct);
    hex("17e8d12cfdf926e0", exp_tag);
    CHECK(nomiq_ccm_encrypt(key, nonce, aad, 8, pt, 23, ct, tag, 8) == 0);
    CHECK(memcmp(ct, exp_ct, 23) == 0);
    CHECK(memcmp(tag, exp_tag, 8) == 0);
    CHECK(nomiq_ccm_decrypt(key, nonce, aad, 8, ct, 23, back, tag, 8) == 0);
    CHECK(memcmp(back, pt, 23) == 0);
    tag[0] ^= 1;
    CHECK(nomiq_ccm_decrypt(key, nonce, aad, 8, ct, 23, back, tag, 8) != 0);
}

static void test_hkdf_rfc5869_case1(void)
{
    uint8_t ikm[22], salt[13], info[10], okm[42], exp[42];
    memset(ikm, 0x0b, sizeof ikm);
    hex("000102030405060708090a0b0c", salt);
    hex("f0f1f2f3f4f5f6f7f8f9", info);
    hex("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865", exp);
    CHECK(nomiq_hkdf_sha256(salt, 13, ikm, 22, info, 10, okm, 42) == 0);
    CHECK(memcmp(okm, exp, 42) == 0);
}

static void test_x25519_rfc7748_and_pairing(void)
{
    uint8_t a[32], b[32], apub[32], bpub[32], exp_apub[32], exp_bpub[32], s1[32], s2[32], exp_s[32];
    hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", a);
    hex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", b);
    hex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", exp_apub);
    hex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", exp_bpub);
    hex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", exp_s);
    CHECK(nomiq_x25519_public(a, apub) == 0 && memcmp(apub, exp_apub, 32) == 0);
    CHECK(nomiq_x25519_public(b, bpub) == 0 && memcmp(bpub, exp_bpub, 32) == 0);
    CHECK(nomiq_x25519_shared(a, bpub, s1) == 0 && memcmp(s1, exp_s, 32) == 0);
    CHECK(nomiq_x25519_shared(b, apub, s2) == 0 && memcmp(s2, exp_s, 32) == 0);

    uint8_t k1[16], k2[16]; uint32_t c1, c2;
    CHECK(nomiq_pair_derive(s1, 0x1111, 0x2222, k1, &c1) == 0);
    CHECK(nomiq_pair_derive(s2, 0x2222, 0x1111, k2, &c2) == 0);  /* order-independent */
    CHECK(memcmp(k1, k2, 16) == 0 && c1 == c2 && c1 < 1000000u);
    printf("  pairing code for RFC 7748 keys: %06u\n", c1);

    /* fresh random keypairs agree */
    uint8_t x[32], y[32], xp[32], yp[32];
    CHECK(nomiq_random(x, 32) == 0 && nomiq_random(y, 32) == 0);
    nomiq_x25519_public(x, xp); nomiq_x25519_public(y, yp);
    nomiq_x25519_shared(x, yp, s1); nomiq_x25519_shared(y, xp, s2);
    CHECK(memcmp(s1, s2, 32) == 0);
}

static void test_frame_seal_open(void)
{
    uint8_t key[16], wrong[16];
    for (int i = 0; i < 16; i++) { key[i] = (uint8_t)i; wrong[i] = (uint8_t)(i + 1); }
    const char *msg = "encrypted hello";
    uint8_t buf[NOMIQ_LORA_MAX_FRAME], copy[NOMIQ_LORA_MAX_FRAME];
    nomiq_hdr_t h = { .type = NOMIQ_T_TEXT, .flags = NOMIQ_F_ACK_REQ, .len = (uint8_t)strlen(msg),
                      .src = 0xAABBCCDD, .dst = 0x11223344, .msg_id = 9, .frag_ix = 0, .frag_n = 1 };
    int n = nomiq_frame_seal(key, 1000, &h, (const uint8_t *)msg, buf, sizeof buf);
    CHECK(n == (int)(NOMIQ_HDR_LEN + strlen(msg) + NOMIQ_SEC_LEN));
    CHECK(memmem(buf, (size_t)n, msg, strlen(msg)) == NULL);   /* no plaintext on air */
    memcpy(copy, buf, (size_t)n);

    nomiq_hdr_t d; const uint8_t *pl; uint32_t ctr;
    CHECK(nomiq_frame_open(key, buf, (size_t)n, &d, &pl, &ctr) == NOMIQ_OK);
    CHECK(ctr == 1000 && d.len == strlen(msg) && memcmp(pl, msg, d.len) == 0);
    CHECK(d.flags & NOMIQ_F_ENCRYPTED);

    memcpy(buf, copy, (size_t)n);
    CHECK(nomiq_frame_open(wrong, buf, (size_t)n, &d, &pl, &ctr) == NOMIQ_E_AUTH);

    /* tamper header field (dst) and fix CRC: MIC must still catch it */
    memcpy(buf, copy, (size_t)n);
    buf[8] ^= 0x01;
    uint16_t crc = nomiq_crc16(buf, 18, 0xFFFF);
    crc = nomiq_crc16(&buf[NOMIQ_HDR_LEN], (size_t)n - NOMIQ_HDR_LEN, crc);
    buf[18] = (uint8_t)crc; buf[19] = (uint8_t)(crc >> 8);
    CHECK(nomiq_frame_open(key, buf, (size_t)n, &d, &pl, &ctr) == NOMIQ_E_AUTH);

    /* bit flip without CRC fix: CRC catches it first */
    memcpy(buf, copy, (size_t)n);
    buf[n - 1] ^= 0x80;
    CHECK(nomiq_frame_open(key, buf, (size_t)n, &d, &pl, &ctr) == NOMIQ_E_CRC);

    /* plaintext frame through open is rejected */
    nomiq_hdr_t p = h; p.flags = 0;
    int m = nomiq_frame_encode(&p, (const uint8_t *)msg, buf, sizeof buf);
    CHECK(nomiq_frame_open(key, buf, (size_t)m, &d, &pl, &ctr) == NOMIQ_E_AUTH);

    /* max payload fits exactly into one LoRa frame */
    uint8_t big[NOMIQ_MAX_PAYLOAD]; memset(big, 0x5A, sizeof big);
    nomiq_hdr_t hb = { .type = NOMIQ_T_DATA, .len = NOMIQ_MAX_PAYLOAD, .src = 1, .dst = 2, .frag_n = 1 };
    CHECK(nomiq_frame_seal(key, 7, &hb, big, buf, sizeof buf) == NOMIQ_LORA_MAX_FRAME);
    CHECK(nomiq_frame_open(key, buf, NOMIQ_LORA_MAX_FRAME, &d, &pl, &ctr) == NOMIQ_OK && !memcmp(pl, big, sizeof big));

    /* same content, different counter => different ciphertext */
    uint8_t b1[NOMIQ_LORA_MAX_FRAME], b2[NOMIQ_LORA_MAX_FRAME];
    nomiq_hdr_t h1 = h, h2 = h;
    nomiq_frame_seal(key, 1, &h1, (const uint8_t *)msg, b1, sizeof b1);
    nomiq_frame_seal(key, 2, &h2, (const uint8_t *)msg, b2, sizeof b2);
    CHECK(memcmp(b1 + 24, b2 + 24, strlen(msg)) != 0);
}

static void test_replay(void)
{
    nomiq_replay_t w = {0};
    CHECK(nomiq_replay_accept(&w, 100));
    CHECK(!nomiq_replay_accept(&w, 100));
    CHECK(nomiq_replay_accept(&w, 105));
    CHECK(nomiq_replay_accept(&w, 101));      /* reordered but fresh */
    CHECK(!nomiq_replay_accept(&w, 101));
    CHECK(nomiq_replay_accept(&w, 200));
    CHECK(!nomiq_replay_accept(&w, 120));     /* older than window */
    CHECK(nomiq_replay_accept(&w, 199));
    nomiq_replay_t z = {0};
    CHECK(nomiq_replay_accept(&z, 0xFFFFFFF0u));
    CHECK(nomiq_replay_accept(&z, 5));            /* counter wrap is treated as newer */
    CHECK(!nomiq_replay_accept(&z, 0xFFFFFFF0u));
}

int main(void)
{
    test_ccm_rfc3610_vector1();
    test_hkdf_rfc5869_case1();
    test_x25519_rfc7748_and_pairing();
    test_frame_seal_open();
    test_replay();
    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
