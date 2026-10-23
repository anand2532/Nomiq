/* Board pin map — MUST match docs/02-hardware-architecture.md §2 (draft v0.1). */
#pragma once

/* Shared SPI2 bus: SX1262, TFT, microSD */
#define PIN_SPI_SCK      39
#define PIN_SPI_MOSI     40
#define PIN_SPI_MISO     41

/* SX1262 */
#define PIN_LORA_NSS     42
#define PIN_LORA_BUSY     2
#define PIN_LORA_DIO1     1
/* LORA_NRST on IO-expander P10 */

/* TFT */
#define PIN_LCD_CS       21
#define PIN_LCD_DC       14
#define PIN_LCD_BL       45   /* strap pin: keep low at reset */

/* microSD (SPI mode) */
#define PIN_SD_CS        38

/* I2C0 — expander, fuel gauge, OV2640 SCCB */
#define PIN_I2C_SDA       4
#define PIN_I2C_SCL       5
#define PIN_IOX_INT       0   /* strap pin: open-drain, idle high */

/* I2S0 full duplex */
#define PIN_I2S_BCLK     47
#define PIN_I2S_WS       48
#define PIN_I2S_DIN      46   /* from mic */
#define PIN_I2S_DOUT      3   /* to amp  */

/* OV2640 DVP (ESP32-S3-EYE compatible) */
#define PIN_CAM_XCLK     15
#define PIN_CAM_VSYNC     6
#define PIN_CAM_HREF      7
#define PIN_CAM_PCLK     13
#define PIN_CAM_D0       11
#define PIN_CAM_D1        9
#define PIN_CAM_D2        8
#define PIN_CAM_D3       10
#define PIN_CAM_D4       12
#define PIN_CAM_D5       18
#define PIN_CAM_D6       17
#define PIN_CAM_D7       16

/* UART0 to STT co-processor */
#define PIN_STT_TX       43
#define PIN_STT_RX       44

/* TCA9535 expander bits (port0 = P00..P07, port1 = P10..P17 -> bit 8..15) */
#define IOX_ADDR          0x20
#define IOX_BTN_UP        0
#define IOX_BTN_DOWN      1
#define IOX_BTN_SELECT    2
#define IOX_BTN_BACK      3
#define IOX_BTN_RECORD    4
#define IOX_BTN_CAMERA    5
#define IOX_CHG_STAT      6
#define IOX_SD_DETECT     7
#define IOX_LORA_NRST     8
#define IOX_LCD_RST       9
#define IOX_CAM_PWDN     10
#define IOX_CAM_RESET    11
#define IOX_AMP_SD       12
#define IOX_STT_EN       13
#define IOX_CAM_PWR_EN   14

/* Radio defaults — see docs/specs/lora-link-budget.md */
#define LORA_FREQ_HZ_DEFAULT   865062500UL
#define LORA_BW_KHZ            125     /* India: carrier BW <= 200 kHz */
#define LORA_SF_CONTROL        9
#define LORA_TX_DBM_MAX        22
