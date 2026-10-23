/*
 * sx126x — portable Semtech SX1262 LoRa driver (command layer).
 *
 * Talks to the chip only through a small HAL (SPI transfer, BUSY wait, reset, delay),
 * so the exact SPI byte sequences are unit-tested on the host against the datasheet
 * (DS.SX1261-2 rev 2.x, §13 commands, §15 errata). The ESP-IDF glue is radio_esp.c.
 */
#ifndef SX126X_H
#define SX126X_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* opcodes */
enum {
    SX_SET_SLEEP = 0x84, SX_SET_STANDBY = 0x80, SX_SET_FS = 0xC1, SX_SET_TX = 0x83, SX_SET_RX = 0x82,
    SX_STOP_TIMER_ON_PREAMBLE = 0x9F, SX_SET_RX_DUTY_CYCLE = 0x94, SX_SET_CAD = 0xC5,
    SX_SET_REGULATOR_MODE = 0x96, SX_CALIBRATE = 0x89, SX_CALIBRATE_IMAGE = 0x98, SX_SET_PA_CONFIG = 0x95,
    SX_SET_RX_TX_FALLBACK = 0x93, SX_WRITE_REGISTER = 0x0D, SX_READ_REGISTER = 0x1D,
    SX_WRITE_BUFFER = 0x0E, SX_READ_BUFFER = 0x1E, SX_SET_DIO_IRQ_PARAMS = 0x08,
    SX_GET_IRQ_STATUS = 0x12, SX_CLEAR_IRQ_STATUS = 0x02, SX_SET_DIO2_RF_SWITCH = 0x9D,
    SX_SET_DIO3_TCXO = 0x97, SX_SET_RF_FREQUENCY = 0x86, SX_SET_PACKET_TYPE = 0x8A,
    SX_SET_TX_PARAMS = 0x8E, SX_SET_MODULATION_PARAMS = 0x8B, SX_SET_PACKET_PARAMS = 0x8C,
    SX_SET_CAD_PARAMS = 0x88, SX_SET_BUFFER_BASE = 0x8F, SX_GET_STATUS = 0xC0,
    SX_GET_RX_BUFFER_STATUS = 0x13, SX_GET_PACKET_STATUS = 0x14, SX_GET_DEVICE_ERRORS = 0x17,
    SX_CLEAR_DEVICE_ERRORS = 0x07,
};

/* IRQ bits */
#define SX_IRQ_TX_DONE        0x0001u
#define SX_IRQ_RX_DONE        0x0002u
#define SX_IRQ_PREAMBLE       0x0004u
#define SX_IRQ_SYNC_WORD      0x0008u
#define SX_IRQ_HEADER_VALID   0x0010u
#define SX_IRQ_HEADER_ERR     0x0020u
#define SX_IRQ_CRC_ERR        0x0040u
#define SX_IRQ_CAD_DONE       0x0080u
#define SX_IRQ_CAD_DETECTED   0x0100u
#define SX_IRQ_TIMEOUT        0x0200u
#define SX_IRQ_ALL            0x03FFu

/* registers */
#define SX_REG_SYNC_WORD_MSB  0x0740u
#define SX_REG_IQ_POLARITY    0x0736u
#define SX_REG_TX_CLAMP       0x08D8u
#define SX_REG_RX_GAIN        0x08ACu
#define SX_REG_OCP            0x08E7u

#define SX_LORA_SYNC_PRIVATE  0x1424u   /* Nomiq uses the private-network sync word */
#define SX_XTAL_HZ            32000000u

typedef struct {
    int  (*xfer)(void *ctx, const uint8_t *tx, uint8_t *rx, size_t n);  /* full-duplex, NSS framed */
    int  (*wait_busy)(void *ctx, uint32_t timeout_ms);                   /* 0 when BUSY low */
    void (*reset)(void *ctx);                                            /* pulse NRESET */
    void (*delay_ms)(void *ctx, uint32_t ms);
    void *ctx;
} sx126x_hal_t;

typedef struct {
    uint32_t freq_hz;
    uint8_t  sf;          /* 5..12 */
    uint16_t bw_khz;      /* 125 (India). 7.8..500 supported by the chip */
    uint8_t  cr;          /* 1..4 => 4/5..4/8 */
    int8_t   tx_dbm;      /* -9..+22 */
    uint16_t preamble;    /* symbols */
    bool     tcxo;        /* TCXO powered from DIO3 */
    uint8_t  tcxo_voltage;/* 0x02 = 1.8 V */
    uint32_t tcxo_delay_us;
    bool     dio2_rf_switch;
    bool     dcdc;
    bool     rx_boost;    /* +~2 dB sensitivity, +~2 mA */
} sx126x_cfg_t;

typedef struct {
    sx126x_hal_t hal;
    sx126x_cfg_t cfg;
    int last_err;
} sx126x_t;

void     sx126x_default_cfg(sx126x_cfg_t *c);
int      sx126x_init(sx126x_t *d, const sx126x_hal_t *hal, const sx126x_cfg_t *cfg);
int      sx126x_set_frequency(sx126x_t *d, uint32_t hz);
int      sx126x_set_lora(sx126x_t *d, uint8_t sf, uint16_t bw_khz, uint8_t cr);
int      sx126x_set_tx_power(sx126x_t *d, int8_t dbm);
int      sx126x_standby(sx126x_t *d, bool xosc);
int      sx126x_sleep(sx126x_t *d, bool warm);
/* Load payload and start TX (returns immediately; wait for TX_DONE on DIO1). */
int      sx126x_transmit(sx126x_t *d, const uint8_t *buf, uint8_t len, uint32_t timeout_ms);
int      sx126x_receive_continuous(sx126x_t *d);
/* Low-power listen: RX for rx_ms, sleep sleep_ms, repeat until preamble detected. */
int      sx126x_receive_duty_cycle(sx126x_t *d, uint32_t rx_ms, uint32_t sleep_ms);
int      sx126x_irq_status(sx126x_t *d, uint16_t *irq);
int      sx126x_irq_clear(sx126x_t *d, uint16_t mask);
int      sx126x_read_packet(sx126x_t *d, uint8_t *buf, uint8_t max, uint8_t *len, int16_t *rssi_dbm,
                            int8_t *snr_db);
int      sx126x_device_errors(sx126x_t *d, uint16_t *errs);
uint32_t sx126x_freq_reg(uint32_t hz);
uint8_t  sx126x_bw_code(uint16_t bw_khz);
bool     sx126x_ldro(uint8_t sf, uint16_t bw_khz);
/* Time on air in microseconds (explicit header, CRC on). */
uint32_t sx126x_toa_us(const sx126x_cfg_t *c, uint8_t len);

#ifdef __cplusplus
}
#endif
#endif /* SX126X_H */
