/* MAX17048 ModelGauge fuel gauge (I2C 0x36). Bus access injected -> host-testable. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define FG_ADDR        0x36
#define FG_REG_VCELL   0x02   /* 78.125 uV / LSB */
#define FG_REG_SOC     0x04   /* 1/256 % / LSB */
#define FG_REG_MODE    0x06
#define FG_REG_VERSION 0x08
#define FG_REG_HIBRT   0x0A
#define FG_REG_CONFIG  0x0C   /* RCOMP[15:8] SLEEP[7] ALSC[6] ALRT[5] ATHD[4:0] */
#define FG_REG_VALRT   0x14
#define FG_REG_CRATE   0x16   /* 0.208 %/h / LSB, signed */
#define FG_REG_STATUS  0x1A
#define FG_REG_CMD     0xFE

typedef struct {
    int (*read16)(uint8_t reg, uint16_t *v, void *ctx);    /* big-endian word on the bus */
    int (*write16)(uint8_t reg, uint16_t v, void *ctx);
    void *ctx;
} fg_bus_t;

int  fg_init(const fg_bus_t *b, uint8_t low_soc_alert_pct);   /* checks VERSION, sets alert threshold */
int  fg_voltage_mv(const fg_bus_t *b, uint16_t *mv);
int  fg_soc(const fg_bus_t *b, uint8_t *pct, uint16_t *pct_x256);
int  fg_charge_rate(const fg_bus_t *b, int16_t *centi_pct_per_h);
int  fg_alert_active(const fg_bus_t *b, bool *active);
int  fg_alert_clear(const fg_bus_t *b);
