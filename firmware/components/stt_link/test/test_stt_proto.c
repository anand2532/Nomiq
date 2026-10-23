#include <stdio.h>
#include <string.h>

#include "stt_proto.h"

static int fails, passes;
#define CHECK(c) do { if (c) passes++; else { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static void hex(const char *s, uint8_t *o, size_t *n)
{
    *n = 0;
    for (; s[0] && s[1]; s += 2) { unsigned v; sscanf(s, "%2x", &v); o[(*n)++] = (uint8_t)v; }
}

static stt_parser_t P;   /* large: keep off the stack */

int main(void)
{
    /* vectors produced by tools/stt-coproc/sttproto.py */
    uint8_t v[64], f[64]; size_t vn;
    hex("a5210d000000690368656c6c6f206e6f6d6971d0ed", v, &vn);
    uint8_t pl[13] = { 0x69, 0x03 }; memcpy(&pl[2], "hello nomiq", 11);   /* conf 0.873 */
    int n = stt_encode(STT_FINAL, pl, 13, f, sizeof f);
    CHECK(n == (int)vn && memcmp(f, v, vn) == 0);
    hex("a51200000000d551", v, &vn);
    n = stt_encode(STT_AUDIO_END, NULL, 0, f, sizeof f);
    CHECK(n == (int)vn && memcmp(f, v, vn) == 0);

    /* parser: garbage + frame + split + corrupted frame + frame */
    stt_parser_init(&P);
    uint8_t stream[256]; size_t sn = 0;
    const uint8_t junk[] = { 0x00, 0xA5, 0xFF, 0x13 };
    memcpy(stream, junk, sizeof junk); sn += sizeof junk;
    n = stt_encode(STT_FINAL, pl, 13, &stream[sn], sizeof stream - sn); sn += (size_t)n;
    size_t bad = sn;
    n = stt_encode(STT_PARTIAL, (const uint8_t *)"abc", 3, &stream[sn], sizeof stream - sn); sn += (size_t)n;
    stream[bad + 7] ^= 0x01;   /* corrupt payload of PARTIAL */
    n = stt_encode(STT_READY, (const uint8_t *)"vosk-small", 10, &stream[sn], sizeof stream - sn); sn += (size_t)n;

    int frames = 0, got_final = 0, got_ready = 0;
    for (size_t i = 0; i < sn; i++) {
        if (stt_parser_feed(&P, stream[i])) {
            frames++;
            if (P.type == STT_FINAL) {
                char t[32]; float c;
                CHECK(stt_final_decode(P.payload, P.len, t, sizeof t, &c) == 11);
                CHECK(strcmp(t, "hello nomiq") == 0 && c > 0.872f && c < 0.874f);
                got_final = 1;
            }
            if (P.type == STT_READY) got_ready = P.len == 10 && !memcmp(P.payload, "vosk-small", 10);
        }
    }
    /* the junk "A5 FF 13 .." starts a bogus header; the parser must still recover */
    CHECK(got_ready);
    CHECK(got_final);                /* recovered although it started inside garbage */
    CHECK(P.crc_errors >= 1);
    printf("  frames=%d final=%d ready=%d crc_errors=%u\n", frames, got_final, got_ready, P.crc_errors);

    /* oversize length is rejected */
    stt_parser_init(&P);
    uint8_t big[] = { 0xA5, 0x11, 0xFF, 0xFF, 0x00, 0x00 };
    for (size_t i = 0; i < sizeof big; i++) CHECK(!stt_parser_feed(&P, big[i]));
    CHECK(stt_encode(STT_AUDIO_CHUNK, v, STT_MAX_PAYLOAD + 1, f, sizeof f) == -1);

    /* UTF-8 safe truncation */
    uint8_t u[2 + 6] = { 0xE8, 0x03, 'a', 'b', 0xE0, 0xA4, 0xA8, 'c' };   /* "ab" + U+0928 + "c" */
    char t[5]; float c;
    CHECK(stt_final_decode(u, sizeof u, t, sizeof t, &c) == 2 && strcmp(t, "ab") == 0 && c == 1.0f);

    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
