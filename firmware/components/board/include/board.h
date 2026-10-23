/* Board support: shared buses and the TCA9535 I/O expander. Pins: board_pins.h */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"

#define BOARD_SPI_HOST SPI2_HOST

/* Initialise I2C0, SPI2 bus and the expander (all outputs safe/off). Idempotent. */
esp_err_t board_init(void);
i2c_master_bus_handle_t board_i2c_bus(void);

/* Expander: bit numbers IOX_* from board_pins.h (0..15) */
esp_err_t iox_set(uint8_t bit, bool level);
esp_err_t iox_read(uint16_t *inputs);

/* Convenience */
void board_lora_reset(void);
uint32_t board_device_id(void);   /* stable 32-bit ID derived from the factory MAC */
