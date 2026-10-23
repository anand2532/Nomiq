/* SX1262 radio abstraction. Implementation: Phase 1 (RadioLib port or native driver). */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

typedef enum { REG_PROFILE_IN865_2008 = 0, REG_PROFILE_IN865_SRD2021 = 1 } radio_reg_profile_t;

typedef struct {
    uint32_t freq_hz;
    uint8_t  sf;          /* 7..12 */
    uint16_t bw_khz;      /* 125 only for India */
    uint8_t  cr;          /* 1..4 => 4/5..4/8 */
    int8_t   tx_dbm;
    radio_reg_profile_t reg_profile;
} radio_cfg_t;

typedef struct { int16_t rssi_dbm; int8_t snr_db; } radio_link_t;
typedef void (*radio_rx_cb_t)(const uint8_t *frame, size_t len, radio_link_t link, void *ctx);

esp_err_t radio_init(const radio_cfg_t *cfg, radio_rx_cb_t cb, void *ctx);
esp_err_t radio_set_cfg(const radio_cfg_t *cfg);
esp_err_t radio_send(const uint8_t *frame, size_t len, uint32_t timeout_ms); /* blocks until TX done */
esp_err_t radio_start_rx(void);
uint32_t  radio_time_on_air_ms(size_t len);
/* Remaining TX airtime budget (ms) under the active regulatory profile; UINT32_MAX if unlimited. */
uint32_t  radio_duty_budget_ms(void);
esp_err_t radio_sleep(void);
