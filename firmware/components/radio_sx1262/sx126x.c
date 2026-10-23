/* sx126x.c — see include/sx126x.h. Command encodings follow DS.SX1261-2 §13. */
#include "sx126x.h"

#include <string.h>

#define BUSY_TIMEOUT_MS 50u

static int cmd(sx126x_t *d, const uint8_t *tx, uint8_t *rx, size_t n)
{
    if (d->hal.wait_busy && d->hal.wait_busy(d->hal.ctx, BUSY_TIMEOUT_MS) != 0) return d->last_err = -2;
    uint8_t dummy[260];
    int rc = d->hal.xfer(d->hal.ctx, tx, rx ? rx : dummy, n);
    if (rc != 0) d->last_err = rc;
    return rc;
}


static int write_reg(sx126x_t *d, uint16_t addr, const uint8_t *v, uint8_t n)
{
    uint8_t b[3 + 16] = { SX_WRITE_REGISTER, (uint8_t)(addr >> 8), (uint8_t)addr };
    memcpy(&b[3], v, n);
    return cmd(d, b, NULL, 3u + n);
}

static int read_reg(sx126x_t *d, uint16_t addr, uint8_t *v, uint8_t n)
{
    uint8_t tx[4 + 16] = { SX_READ_REGISTER, (uint8_t)(addr >> 8), (uint8_t)addr, 0 };
    uint8_t rx[4 + 16];
    int rc = cmd(d, tx, rx, 4u + n);
    if (rc == 0) memcpy(v, &rx[4], n);
    return rc;
}

void sx126x_default_cfg(sx126x_cfg_t *c)
{
    memset(c, 0, sizeof(*c));
    c->freq_hz = 865062500u;
    c->sf = 9; c->bw_khz = 125; c->cr = 1;
    c->tx_dbm = 22; c->preamble = 8;
    c->tcxo = true; c->tcxo_voltage = 0x02; c->tcxo_delay_us = 5000;
    c->dio2_rf_switch = true; c->dcdc = true; c->rx_boost = true;
}

uint32_t sx126x_freq_reg(uint32_t hz) { return (uint32_t)(((uint64_t)hz << 25) / SX_XTAL_HZ); }

uint8_t sx126x_bw_code(uint16_t bw_khz)
{
    switch (bw_khz) {
    case 7: return 0x00;  case 10: return 0x08; case 15: return 0x01; case 20: return 0x09;
    case 31: return 0x02; case 41: return 0x0A; case 62: return 0x03; case 125: return 0x04;
    case 250: return 0x05; case 500: return 0x06; default: return 0x04;
    }
}

bool sx126x_ldro(uint8_t sf, uint16_t bw_khz)
{
    /* symbol time >= 16.38 ms */
    return ((1000000u << sf) / ((uint32_t)bw_khz * 1000u)) >= 16380u;
}

int sx126x_set_frequency(sx126x_t *d, uint32_t hz)
{
    uint32_t f = sx126x_freq_reg(hz);
    uint8_t b[5] = { SX_SET_RF_FREQUENCY, (uint8_t)(f >> 24), (uint8_t)(f >> 16), (uint8_t)(f >> 8), (uint8_t)f };
    d->cfg.freq_hz = hz;
    return cmd(d, b, NULL, sizeof b);
}

static int set_packet_params(sx126x_t *d, uint8_t payload_len)
{
    uint8_t b[7] = { SX_SET_PACKET_PARAMS, (uint8_t)(d->cfg.preamble >> 8), (uint8_t)d->cfg.preamble,
                     0x00 /* explicit header */, payload_len, 0x01 /* CRC on */, 0x00 /* standard IQ */ };
    int rc = cmd(d, b, NULL, sizeof b);
    if (rc) return rc;
    /* errata 15.4: optimise IQ polarity register for standard IQ */
    uint8_t v;
    if ((rc = read_reg(d, SX_REG_IQ_POLARITY, &v, 1)) != 0) return rc;
    v |= 0x04;
    return write_reg(d, SX_REG_IQ_POLARITY, &v, 1);
}

int sx126x_set_lora(sx126x_t *d, uint8_t sf, uint16_t bw_khz, uint8_t cr)
{
    d->cfg.sf = sf; d->cfg.bw_khz = bw_khz; d->cfg.cr = cr;
    uint8_t b[5] = { SX_SET_MODULATION_PARAMS, sf, sx126x_bw_code(bw_khz), cr, sx126x_ldro(sf, bw_khz) ? 1 : 0 };
    return cmd(d, b, NULL, sizeof b);
}

int sx126x_set_tx_power(sx126x_t *d, int8_t dbm)
{
    if (dbm > 22) dbm = 22;
    if (dbm < -9) dbm = -9;
    d->cfg.tx_dbm = dbm;
    /* SX1262 optimal PA settings (DS §13.1.14, table 13-21) */
    uint8_t duty, hpmax;
    if (dbm > 20)      { duty = 0x04; hpmax = 0x07; }
    else if (dbm > 17) { duty = 0x03; hpmax = 0x05; }
    else if (dbm > 14) { duty = 0x02; hpmax = 0x03; }
    else               { duty = 0x02; hpmax = 0x02; }
    uint8_t pa[5] = { SX_SET_PA_CONFIG, duty, hpmax, 0x00 /* SX1262 */, 0x01 };
    int rc = cmd(d, pa, NULL, sizeof pa);
    if (rc) return rc;
    uint8_t tx[3] = { SX_SET_TX_PARAMS, (uint8_t)dbm, 0x04 /* 200 us ramp */ };
    return cmd(d, tx, NULL, sizeof tx);
}

int sx126x_standby(sx126x_t *d, bool xosc)
{
    uint8_t b[2] = { SX_SET_STANDBY, xosc ? 0x01 : 0x00 };
    return cmd(d, b, NULL, 2);
}

int sx126x_sleep(sx126x_t *d, bool warm)
{
    uint8_t b[2] = { SX_SET_SLEEP, warm ? 0x04 : 0x00 };
    return cmd(d, b, NULL, 2);
}

int sx126x_init(sx126x_t *d, const sx126x_hal_t *hal, const sx126x_cfg_t *cfg)
{
    memset(d, 0, sizeof(*d));
    d->hal = *hal;
    d->cfg = *cfg;
    int rc = 0;
    if (d->hal.reset) d->hal.reset(d->hal.ctx);
    if ((rc = sx126x_standby(d, false))) return rc;
    if (cfg->tcxo) {
        uint32_t t = (uint32_t)((uint64_t)cfg->tcxo_delay_us * 64u / 1000u);  /* 15.625 us steps */
        uint8_t b[5] = { SX_SET_DIO3_TCXO, cfg->tcxo_voltage, (uint8_t)(t >> 16), (uint8_t)(t >> 8), (uint8_t)t };
        if ((rc = cmd(d, b, NULL, sizeof b))) return rc;
        uint8_t clr[3] = { SX_CLEAR_DEVICE_ERRORS, 0, 0 };   /* XOSC_START_ERR expected before TCXO */
        if ((rc = cmd(d, clr, NULL, sizeof clr))) return rc;
    }
    uint8_t cal[2] = { SX_CALIBRATE, 0x7F };
    if ((rc = cmd(d, cal, NULL, sizeof cal))) return rc;
    if (d->hal.delay_ms) d->hal.delay_ms(d->hal.ctx, 5);
    uint8_t reg[2] = { SX_SET_REGULATOR_MODE, cfg->dcdc ? 0x01 : 0x00 };
    if ((rc = cmd(d, reg, NULL, sizeof reg))) return rc;
    if (cfg->dio2_rf_switch) {
        uint8_t b[2] = { SX_SET_DIO2_RF_SWITCH, 0x01 };
        if ((rc = cmd(d, b, NULL, sizeof b))) return rc;
    }
    uint8_t pt[2] = { SX_SET_PACKET_TYPE, 0x01 /* LoRa */ };
    if ((rc = cmd(d, pt, NULL, sizeof pt))) return rc;
    if ((rc = sx126x_set_frequency(d, cfg->freq_hz))) return rc;
    /* image calibration for 863–870 MHz band (DS table 9-2) */
    uint8_t ci[3] = { SX_CALIBRATE_IMAGE, 0xD7, 0xDB };
    if ((rc = cmd(d, ci, NULL, sizeof ci))) return rc;
    if ((rc = sx126x_set_tx_power(d, cfg->tx_dbm))) return rc;
    uint8_t base[3] = { SX_SET_BUFFER_BASE, 0x00, 0x00 };
    if ((rc = cmd(d, base, NULL, sizeof base))) return rc;
    if ((rc = sx126x_set_lora(d, cfg->sf, cfg->bw_khz, cfg->cr))) return rc;
    if ((rc = set_packet_params(d, 0xFF))) return rc;
    uint8_t sw[2] = { (uint8_t)(SX_LORA_SYNC_PRIVATE >> 8), (uint8_t)SX_LORA_SYNC_PRIVATE };
    if ((rc = write_reg(d, SX_REG_SYNC_WORD_MSB, sw, 2))) return rc;
    /* errata 15.2: better resistance to antenna mismatch */
    uint8_t clamp;
    if ((rc = read_reg(d, SX_REG_TX_CLAMP, &clamp, 1))) return rc;
    clamp |= 0x1E;
    if ((rc = write_reg(d, SX_REG_TX_CLAMP, &clamp, 1))) return rc;
    if (cfg->rx_boost) {
        uint8_t g = 0x96;
        if ((rc = write_reg(d, SX_REG_RX_GAIN, &g, 1))) return rc;
    }
    uint16_t m = SX_IRQ_TX_DONE | SX_IRQ_RX_DONE | SX_IRQ_TIMEOUT | SX_IRQ_CRC_ERR | SX_IRQ_HEADER_ERR |
                 SX_IRQ_PREAMBLE | SX_IRQ_CAD_DONE | SX_IRQ_CAD_DETECTED;
    uint8_t irq[9] = { SX_SET_DIO_IRQ_PARAMS, (uint8_t)(m >> 8), (uint8_t)m, (uint8_t)(m >> 8), (uint8_t)m, 0, 0, 0, 0 };
    if ((rc = cmd(d, irq, NULL, sizeof irq))) return rc;
    uint8_t fb[2] = { SX_SET_RX_TX_FALLBACK, 0x30 /* STDBY_XOSC: faster turnaround */ };
    return cmd(d, fb, NULL, sizeof fb);
}

static int set_timeout_cmd(sx126x_t *d, uint8_t op, uint32_t timeout_ms)
{
    uint32_t t = timeout_ms ? (uint32_t)((uint64_t)timeout_ms * 64u) : 0u;  /* 15.625 us units */
    if (t > 0xFFFFFEu) t = 0xFFFFFEu;
    uint8_t b[4] = { op, (uint8_t)(t >> 16), (uint8_t)(t >> 8), (uint8_t)t };
    return cmd(d, b, NULL, sizeof b);
}

int sx126x_transmit(sx126x_t *d, const uint8_t *buf, uint8_t len, uint32_t timeout_ms)
{
    int rc;
    if ((rc = sx126x_standby(d, false))) return rc;
    if ((rc = set_packet_params(d, len))) return rc;
    uint8_t hdr[2] = { SX_WRITE_BUFFER, 0x00 };
    uint8_t b[2 + 255];
    memcpy(b, hdr, 2);
    memcpy(&b[2], buf, len);
    if ((rc = cmd(d, b, NULL, 2u + len))) return rc;
    if ((rc = sx126x_irq_clear(d, SX_IRQ_ALL))) return rc;
    return set_timeout_cmd(d, SX_SET_TX, timeout_ms);
}

int sx126x_receive_continuous(sx126x_t *d)
{
    int rc;
    if ((rc = set_packet_params(d, 0xFF))) return rc;
    if ((rc = sx126x_irq_clear(d, SX_IRQ_ALL))) return rc;
    uint8_t b[4] = { SX_SET_RX, 0xFF, 0xFF, 0xFF };
    return cmd(d, b, NULL, sizeof b);
}

int sx126x_receive_duty_cycle(sx126x_t *d, uint32_t rx_ms, uint32_t sleep_ms)
{
    int rc;
    if ((rc = set_packet_params(d, 0xFF))) return rc;
    if ((rc = sx126x_irq_clear(d, SX_IRQ_ALL))) return rc;
    uint32_t r = rx_ms * 64u, s = sleep_ms * 64u;
    uint8_t b[7] = { SX_SET_RX_DUTY_CYCLE, (uint8_t)(r >> 16), (uint8_t)(r >> 8), (uint8_t)r,
                     (uint8_t)(s >> 16), (uint8_t)(s >> 8), (uint8_t)s };
    return cmd(d, b, NULL, sizeof b);
}

int sx126x_irq_status(sx126x_t *d, uint16_t *irq)
{
    uint8_t tx[4] = { SX_GET_IRQ_STATUS, 0, 0, 0 }, rx[4];
    int rc = cmd(d, tx, rx, 4);
    if (rc == 0) *irq = (uint16_t)((rx[2] << 8) | rx[3]);
    return rc;
}

int sx126x_irq_clear(sx126x_t *d, uint16_t mask)
{
    uint8_t b[3] = { SX_CLEAR_IRQ_STATUS, (uint8_t)(mask >> 8), (uint8_t)mask };
    return cmd(d, b, NULL, 3);
}

int sx126x_read_packet(sx126x_t *d, uint8_t *buf, uint8_t max, uint8_t *len, int16_t *rssi_dbm, int8_t *snr_db)
{
    uint8_t tx[4] = { SX_GET_RX_BUFFER_STATUS, 0, 0, 0 }, rx[4];
    int rc = cmd(d, tx, rx, 4);
    if (rc) return rc;
    uint8_t n = rx[2], start = rx[3];
    if (n > max) n = max;
    uint8_t rt[3 + 255] = { SX_READ_BUFFER, start, 0 }, rr[3 + 255];
    if ((rc = cmd(d, rt, rr, 3u + n))) return rc;
    memcpy(buf, &rr[3], n);
    *len = n;
    uint8_t pt[5] = { SX_GET_PACKET_STATUS, 0, 0, 0, 0 }, pr[5];
    if ((rc = cmd(d, pt, pr, 5))) return rc;
    if (rssi_dbm) *rssi_dbm = (int16_t)(-(int16_t)pr[2] / 2);
    if (snr_db) *snr_db = (int8_t)(((int8_t)pr[3] + 2) / 4);
    return 0;
}

int sx126x_device_errors(sx126x_t *d, uint16_t *errs)
{
    uint8_t tx[4] = { SX_GET_DEVICE_ERRORS, 0, 0, 0 }, rx[4];
    int rc = cmd(d, tx, rx, 4);
    if (rc == 0) *errs = (uint16_t)((rx[2] << 8) | rx[3]);
    return rc;
}

uint32_t sx126x_toa_us(const sx126x_cfg_t *c, uint8_t len)
{
    /* DS §6.1.4 — computed in microsymbols to stay in integer math */
    uint32_t sf = c->sf, cr = c->cr;
    uint32_t tsym_us = (uint32_t)((1000000ull << sf) / ((uint64_t)c->bw_khz * 1000u));
    uint32_t ldro = sx126x_ldro(c->sf, c->bw_khz) ? 1u : 0u;
    int32_t num, den;
    uint32_t pre_x4;
    if (sf <= 6) {
        num = 8 * (int32_t)len + 16 - 4 * (int32_t)sf;
        den = 4 * (int32_t)sf;
        pre_x4 = (c->preamble * 4u) + 25u;     /* +6.25 */
    } else {
        num = 8 * (int32_t)len + 16 - 4 * (int32_t)sf + 8;
        den = 4 * ((int32_t)sf - 2 * (int32_t)ldro);
        pre_x4 = (c->preamble * 4u) + 17u;     /* +4.25 */
    }
    int32_t q = num > 0 ? (num + den - 1) / den : 0;
    uint32_t nsym_x4 = pre_x4 + 4u * (8u + (uint32_t)q * (cr + 4u));
    return (uint32_t)(((uint64_t)nsym_x4 * tsym_us) / 4u);
}
