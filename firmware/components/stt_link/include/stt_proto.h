/* stt_proto — UART framing between the ESP32-S3 and the STT co-processor.
 * Frame: 0xA5 | type u8 | len u32 LE | payload | crc16 LE (CCITT-FALSE over type+len+payload)
 * Python mirror: tools/stt-coproc/sttproto.py (shared test vectors). Pure C, host-tested. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STT_SOF          0xA5u
#define STT_MAX_PAYLOAD  4096u
#define STT_OVERHEAD     8u

enum {
    STT_HELLO = 0x01, STT_READY = 0x02,
    STT_AUDIO_BEGIN = 0x10, STT_AUDIO_CHUNK = 0x11, STT_AUDIO_END = 0x12,
    STT_PARTIAL = 0x20, STT_FINAL = 0x21,
    STT_SHUTDOWN = 0x30,
    STT_ERROR = 0x7F,
};

uint16_t stt_crc16(const uint8_t *d, size_t n, uint16_t crc);
/* Returns frame length, or -1 if it does not fit. */
int stt_encode(uint8_t type, const uint8_t *payload, uint32_t len, uint8_t *out, size_t cap);

typedef struct {
    uint8_t  raw[STT_MAX_PAYLOAD + STT_OVERHEAD];  /* candidate frame bytes */
    uint32_t n;              /* bytes in raw */
    uint32_t consume;        /* bytes of the last returned frame, dropped on next feed */
    uint8_t  type;           /* valid after feed() returns true */
    uint32_t len;
    const uint8_t *payload;  /* points into raw; valid until the next feed() */
    uint32_t crc_errors;
} stt_parser_t;

void stt_parser_init(stt_parser_t *p);
/* Feed one byte. Returns true when a complete, valid frame is available in p->type/payload/len.
 * On a bad header/CRC the parser rescans the buffered bytes for the next SOF, so a frame
 * that starts inside garbage is still recovered. */
bool stt_parser_feed(stt_parser_t *p, uint8_t b);

/* FINAL payload helpers */
int stt_final_decode(const uint8_t *p, uint32_t len, char *text, size_t cap, float *confidence);
