#include "fuel_gauge.h"

int fg_init(const fg_bus_t *b, uint8_t low_pct)
{
    uint16_t v, cfg;
    if (b->read16(FG_REG_VERSION, &v, b->ctx) != 0) return -1;
    if ((v & 0xFFF0) != 0x0010) return -2;                    /* MAX17048/49: 0x001x */
    if (b->read16(FG_REG_CONFIG, &cfg, b->ctx) != 0) return -1;
    if (low_pct < 1) low_pct = 1;
    if (low_pct > 32) low_pct = 32;
    cfg = (uint16_t)((cfg & 0xFF00) | (uint16_t)(32 - low_pct));   /* ATHD = 32 - pct, clears ALRT */
    return b->write16(FG_REG_CONFIG, cfg, b->ctx);
}

int fg_voltage_mv(const fg_bus_t *b, uint16_t *mv)
{
    uint16_t r;
    if (b->read16(FG_REG_VCELL, &r, b->ctx) != 0) return -1;
    *mv = (uint16_t)(((uint32_t)r * 78125u + 500000u) / 1000000u);
    return 0;
}

int fg_soc(const fg_bus_t *b, uint8_t *pct, uint16_t *x256)
{
    uint16_t r;
    if (b->read16(FG_REG_SOC, &r, b->ctx) != 0) return -1;
    if (x256) *x256 = r;
    uint32_t p = (r + 128u) >> 8;
    *pct = (uint8_t)(p > 100 ? 100 : p);                       /* SOC can read slightly > 100 % */
    return 0;
}

int fg_charge_rate(const fg_bus_t *b, int16_t *c)
{
    uint16_t r;
    if (b->read16(FG_REG_CRATE, &r, b->ctx) != 0) return -1;
    *c = (int16_t)(((int32_t)(int16_t)r * 208) / 10);          /* 0.208 %/h -> 0.01 %/h units */
    return 0;
}

int fg_alert_active(const fg_bus_t *b, bool *a)
{
    uint16_t cfg;
    if (b->read16(FG_REG_CONFIG, &cfg, b->ctx) != 0) return -1;
    *a = (cfg & 0x0020) != 0;
    return 0;
}

int fg_alert_clear(const fg_bus_t *b)
{
    uint16_t cfg;
    if (b->read16(FG_REG_CONFIG, &cfg, b->ctx) != 0) return -1;
    return b->write16(FG_REG_CONFIG, (uint16_t)(cfg & ~0x0020u), b->ctx);
}
