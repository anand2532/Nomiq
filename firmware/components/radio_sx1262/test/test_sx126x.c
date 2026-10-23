/* Host tests: verify SX1262 SPI command bytes against the datasheet using a mock HAL. */
#include <stdio.h>
#include <string.h>

#include "sx126x.h"

static int fails, passes;
#define CHECK(c) do { if (c) passes++; else { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

typedef struct { uint8_t b[260]; size_t n; } xfer_t;
static xfer_t log_[256];
static int nlog, resets, busy_fail;

static int m_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t n)
{
    (void)ctx;
    if (nlog < 256) { memcpy(log_[nlog].b, tx, n); log_[nlog].n = n; nlog++; }
    memset(rx, 0, n);
    switch (tx[0]) {
    case SX_GET_RX_BUFFER_STATUS: rx[2] = 5; rx[3] = 0x80; break;
    case SX_READ_BUFFER: for (size_t i = 3; i < n; i++) rx[i] = (uint8_t)(0xA0 + i - 3); break;
    case SX_GET_PACKET_STATUS: rx[2] = 180; rx[3] = 40; break;         /* -90 dBm, +10 dB */
    case SX_GET_IRQ_STATUS: rx[2] = 0x00; rx[3] = 0x02; break;         /* RX_DONE */
    case SX_READ_REGISTER: rx[4] = 0x01; break;
    default: break;
    }
    return 0;
}
static int m_busy(void *ctx, uint32_t t) { (void)ctx; (void)t; return busy_fail ? -1 : 0; }
static void m_reset(void *ctx) { (void)ctx; resets++; }
static void m_delay(void *ctx, uint32_t ms) { (void)ctx; (void)ms; }

static const sx126x_hal_t hal = { m_xfer, m_busy, m_reset, m_delay, NULL };

static int find(uint8_t op, int from)
{
    for (int i = from; i < nlog; i++) if (log_[i].b[0] == op) return i;
    return -1;
}
static int eq(int i, const uint8_t *exp, size_t n) { return i >= 0 && log_[i].n == n && !memcmp(log_[i].b, exp, n); }

int main(void)
{
    sx126x_t d; sx126x_cfg_t c;
    sx126x_default_cfg(&c);
    nlog = 0;
    CHECK(sx126x_init(&d, &hal, &c) == 0);
    CHECK(resets == 1);
    { uint8_t e[] = { 0x80, 0x00 };                   CHECK(eq(0, e, 2)); }      /* STDBY_RC first */
    { uint8_t e[] = { 0x97, 0x02, 0x00, 0x01, 0x40 }; CHECK(eq(find(0x97, 0), e, 5)); } /* 1.8 V, 5 ms */
    { uint8_t e[] = { 0x89, 0x7F };                   CHECK(eq(find(0x89, 0), e, 2)); }
    { uint8_t e[] = { 0x96, 0x01 };                   CHECK(eq(find(0x96, 0), e, 2)); }  /* DC-DC */
    { uint8_t e[] = { 0x9D, 0x01 };                   CHECK(eq(find(0x9D, 0), e, 2)); }
    { uint8_t e[] = { 0x8A, 0x01 };                   CHECK(eq(find(0x8A, 0), e, 2)); }  /* LoRa */
    /* 865.0625 MHz -> 865062500 * 2^25 / 32e6 = 907083776 = 0x36110000 */
    CHECK(sx126x_freq_reg(865062500u) == 907083776u);
    { uint8_t e[] = { 0x86, 0x36, 0x11, 0x00, 0x00 }; CHECK(eq(find(0x86, 0), e, 5)); }
    { uint8_t e[] = { 0x98, 0xD7, 0xDB };             CHECK(eq(find(0x98, 0), e, 3)); }  /* 863-870 image cal */
    { uint8_t e[] = { 0x95, 0x04, 0x07, 0x00, 0x01 }; CHECK(eq(find(0x95, 0), e, 5)); }  /* +22 dBm PA */
    { uint8_t e[] = { 0x8E, 22, 0x04 };               CHECK(eq(find(0x8E, 0), e, 3)); }
    { uint8_t e[] = { 0x8B, 9, 0x04, 0x01, 0x00 };    CHECK(eq(find(0x8B, 0), e, 5)); }  /* SF9 BW125 4/5 */
    { uint8_t e[] = { 0x8C, 0x00, 0x08, 0x00, 0xFF, 0x01, 0x00 }; CHECK(eq(find(0x8C, 0), e, 7)); }
    { uint8_t e[] = { 0x0D, 0x07, 0x40, 0x14, 0x24 }; CHECK(eq(find(0x0D, find(0x8C, 0)), e, 5) ||
                                                            eq(find(0x0D, find(0x0D, find(0x8C, 0)) + 1), e, 5)); }
    int clamp = -1;
    for (int i = 0; i < nlog; i++) if (log_[i].b[0] == 0x0D && log_[i].b[1] == 0x08 && log_[i].b[2] == 0xD8) clamp = i;
    CHECK(clamp >= 0 && log_[clamp].b[3] == (0x01 | 0x1E));    /* errata 15.2 */
    int iq = -1;
    for (int i = 0; i < nlog; i++) if (log_[i].b[0] == 0x0D && log_[i].b[1] == 0x07 && log_[i].b[2] == 0x36) iq = i;
    CHECK(iq >= 0 && (log_[iq].b[3] & 0x04));                   /* errata 15.4 */
    int irq = find(0x08, 0);
    CHECK(irq >= 0 && log_[irq].n == 9 && log_[irq].b[2] & 0x01 && log_[irq].b[2] & 0x02);

    /* power table */
    nlog = 0; sx126x_set_tx_power(&d, 14);
    { uint8_t e[] = { 0x95, 0x02, 0x02, 0x00, 0x01 }; CHECK(eq(0, e, 5)); }
    { uint8_t e[] = { 0x8E, 14, 0x04 };               CHECK(eq(1, e, 3)); }
    nlog = 0; sx126x_set_tx_power(&d, 30);
    CHECK(log_[1].b[1] == 22);                                   /* clamped */

    /* LDRO */
    CHECK(!sx126x_ldro(10, 125) && sx126x_ldro(11, 125) && sx126x_ldro(12, 125) && !sx126x_ldro(12, 500));
    nlog = 0; sx126x_set_lora(&d, 12, 125, 1);
    { uint8_t e[] = { 0x8B, 12, 0x04, 0x01, 0x01 };   CHECK(eq(0, e, 5)); }
    sx126x_set_lora(&d, 9, 125, 1);

    /* transmit */
    uint8_t pkt[4] = { 1, 2, 3, 4 };
    nlog = 0;
    CHECK(sx126x_transmit(&d, pkt, 4, 1000) == 0);
    { uint8_t e[] = { 0x8C, 0x00, 0x08, 0x00, 0x04, 0x01, 0x00 }; CHECK(eq(find(0x8C, 0), e, 7)); }
    { uint8_t e[] = { 0x0E, 0x00, 1, 2, 3, 4 };      CHECK(eq(find(0x0E, 0), e, 6)); }
    { uint8_t e[] = { 0x83, 0x00, 0xFA, 0x00 };      CHECK(eq(nlog - 1, e, 4)); }  /* 1000 ms = 64000 */
    CHECK(find(0x02, 0) >= 0 && find(0x02, 0) < nlog - 1);                       /* IRQ cleared before TX */

    /* RX continuous / duty-cycle */
    nlog = 0; sx126x_receive_continuous(&d);
    { uint8_t e[] = { 0x82, 0xFF, 0xFF, 0xFF };      CHECK(eq(nlog - 1, e, 4)); }
    nlog = 0; sx126x_receive_duty_cycle(&d, 10, 100);
    { uint8_t e[] = { 0x94, 0x00, 0x02, 0x80, 0x00, 0x19, 0x00 }; CHECK(eq(nlog - 1, e, 7)); }

    /* read packet */
    uint8_t buf[32], len; int16_t rssi; int8_t snr;
    nlog = 0;
    CHECK(sx126x_read_packet(&d, buf, sizeof buf, &len, &rssi, &snr) == 0);
    CHECK(len == 5 && buf[0] == 0xA0 && buf[4] == 0xA4);
    { uint8_t e[] = { 0x1E, 0x80, 0x00 };            CHECK(log_[1].b[0] == e[0] && log_[1].b[1] == e[1] && log_[1].n == 8); }
    CHECK(rssi == -90 && snr == 10);
    uint16_t st; CHECK(sx126x_irq_status(&d, &st) == 0 && st == SX_IRQ_RX_DONE);

    /* busy timeout propagates */
    busy_fail = 1; CHECK(sx126x_standby(&d, true) != 0); busy_fail = 0;

    /* time on air vs Semtech formula (python tools/lora_airtime.py) */
    sx126x_cfg_t t = c;
    t.sf = 7;  CHECK(sx126x_toa_us(&t, 10) == 36096);            /* 35.25 symbols x 1.024 ms */
    t.sf = 12; CHECK(sx126x_toa_us(&t, 10) / 1000 == 991);      /* 991.2 ms */
    t.sf = 9;  CHECK(sx126x_toa_us(&t, 255) / 1000 == 1250);    /* 1250.3 ms */
    t.sf = 7;  CHECK(sx126x_toa_us(&t, 255) / 1000 == 394);
    printf("  ToA SF7/125 255 B = %u us\n", sx126x_toa_us(&t, 255));

    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
