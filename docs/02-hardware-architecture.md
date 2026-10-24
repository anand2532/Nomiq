# 02 — Hardware Architecture

> Status: **DRAFT v0.1** — pin map must be re-checked against the ESP32-S3-WROOM-1 datasheet and esp32-camera driver before schematic capture. Owner: `electronics-schematic-engineer` agent / HW lead.

## 1. Module choice

**ESP32-S3-WROOM-1-N16R8** (16 MB QSPI flash, 8 MB **octal** PSRAM).
Octal PSRAM consumes GPIO35/36/37 → not available. USB D-/D+ on GPIO19/20 used for USB-Serial-JTAG (programming + console), so UART0 (GPIO43/44) is free for the STT co-processor link.

## 2. GPIO allocation (draft)

| GPIO | Function | Dir | Notes |
|---|---|---|---|
| 0 | IOX_INT (expander interrupt) | in | **Strap (boot mode).** Expander INT is open-drain and idle-high at reset; 10 k pull-up. Also wired to BOOT test pad. |
| 1 | LORA_DIO1 | in | IRQ, wake source (RTC GPIO) |
| 2 | LORA_BUSY | in | |
| 3 | I2S_DOUT → amp | out | Strap (JTAG sel) — only active if eFuse set; amp DIN is high-Z |
| 4 | I2C_SDA (+ camera SCCB) | io | 2.2 k pull-up; shared bus |
| 5 | I2C_SCL (+ camera SCCB) | out | |
| 6 | CAM_VSYNC | in | |
| 7 | CAM_HREF | in | |
| 8 | CAM_D2 | in | |
| 9 | CAM_D1 | in | |
| 10 | CAM_D3 | in | |
| 11 | CAM_D0 | in | |
| 12 | CAM_D4 | in | |
| 13 | CAM_PCLK | in | |
| 14 | LCD_DC | out | |
| 15 | CAM_XCLK | out | 20 MHz LEDC |
| 16 | CAM_D7 | in | |
| 17 | CAM_D6 | in | |
| 18 | CAM_D5 | in | |
| 19 / 20 | USB D- / D+ | io | ESD + 22 Ω? (check S3 ref) |
| 21 | LCD_CS | out | |
| 38 | SD_CS | out | |
| 39 | SPI_SCK (LoRa, LCD, SD) | out | Shared SPI2 bus, see §3 |
| 40 | SPI_MOSI | out | |
| 41 | SPI_MISO | in | |
| 42 | LORA_NSS | out | |
| 43 / 44 | UART0 TX/RX → STT co-proc | io | Level-compatible 3V3 |
| 45 | LCD_BL (PWM) | out | **Strap (VDD_SPI).** Must be low at reset → gate pulldown on backlight MOSFET |
| 46 | I2S_DIN ← mic | in | Strap (ROM log); mic SD is tri-state when unclocked |
| 47 | I2S_BCLK (mic + amp) | out | |
| 48 | I2S_WS (mic + amp) | out | |

Camera pinout follows the ESP32-S3-EYE reference so `esp32-camera` defaults work. All 31 usable GPIOs are consumed — **any new signal must go to the I²C expander.**

### Expander (TCA9535 / PCA9535, 16-bit, I²C 0x20)

| Pin | Signal | Pin | Signal |
|---|---|---|---|
| P00 | BTN_UP | P10 | LORA_NRST |
| P01 | BTN_DOWN | P11 | LCD_RST |
| P02 | BTN_SELECT | P12 | CAM_PWDN |
| P03 | BTN_BACK | P13 | CAM_RESET |
| P04 | BTN_RECORD (PTT) | P14 | AMP_SD (shutdown) |
| P05 | BTN_CAMERA | P15 | STT_EN (co-proc load switch) |
| P06 | CHG_STAT | P16 | CAM_PWR_EN |
| P07 | SD_DETECT | P17 | spare / LED |

Power button goes to the charger/PMIC "ship mode / power-on" input and a sense line (not a GPIO).

## 3. Bus plan & conflicts

| Bus | Devices | Max clock | Risk / mitigation |
|---|---|---|---|
| SPI2 | SX1262, TFT, microSD | 10 MHz LoRa / 40 MHz LCD / 20 MHz SD | LCD frame push (~150 kB) blocks bus ~30 ms; SX1262 buffers RX in its FIFO and signals DIO1, so no packet loss. Use `spi_device` per-chip + bus lock. **Fallback:** move SD to SDMMC 1-bit if a spare GPIO is freed. |
| I²C0 | Expander, fuel gauge, OV2640 SCCB, (charger) | 400 kHz | Camera SCCB shares port (`sccb_i2c_port`). |
| I²S0 | Mic RX + amp TX (full-duplex, shared BCLK/WS) | 16 kHz × 32 bit | Same sample rate for both — 16 kHz. |
| UART0 | STT co-processor | 921 600 bd | 16 kHz 16-bit PCM = 256 kbit/s → OK |

## 4. Power tree

```
USB-C (5 V, CC 5.1 k pulldowns, TVS)
  └─ Charger w/ power-path BQ24074 (1.0 A charge, 1.42 A input limit, 5 h timer)
        ├─ BAT: 1S Li-ion 3000 mAh, protection IC (DW01+FS8205 or in-pack PCM), NTC
        └─ VSYS 3.4–4.4 V
              ├─ 3V3 buck-boost TPS63001 (fixed 3.3 V; 1.2 A buck / 0.8 A boost at 2.4 V) → ESP32, LoRa, LCD, SD, mic, IOX
              ├─ AMP supply directly from VSYS (class-D, efficiency)
              ├─ CAM rails: TPS22918 switch (CAM_PWR_EN) → TLV75528 2.8 V AVDD, TLV75512 1.2 V DVDD, DOVDD 3V3
              └─ STT co-proc: VSYS load switch TPS22918 behind STT_EN (5 V boost only if the SoM needs it)
Fuel gauge (MAX17048 class) on BAT
```

Peak budget: ESP32 Wi-Fi off ~100 mA, SX1262 TX @ +22 dBm ~120 mA, LCD+BL ~60 mA, camera ~100 mA, amp up to 500 mA peaks, STT SoM 300–600 mA. Never run camera + STT + amp peak simultaneously (firmware interlock).

## 5. RF front-end

* SX1262 with **TCXO** (DIO3-controlled) for frequency stability on long packets.
* Follow Semtech reference matching for 868 MHz band (865–867 used), high-power PA path; DIO2 controls RF switch.
* 50 Ω CPWG trace on L1 over solid L2 ground, via fence, π-network for antenna tuning, u.FL + bulkhead SMA.
* Keep ≥ 10 mm from buck-boost inductor and camera FPC; shield can footprint over SX1262 + matching.

## 6. Layer stack (4-layer, JLCPCB JLC04161H-7628 or equivalent)

| Layer | Use |
|---|---|
| L1 | Components, RF, high-speed (DVP, SPI) |
| L2 | Solid GND |
| L3 | Power (3V3, VSYS pours) + low-speed routing |
| L4 | Routing, GND fill, buttons, test points |

## 7. Connectors & UI hardware

USB-C (16-pin, USB2 + CC), battery JST-PH 2-pin, u.FL, camera 24-pin FPC, LCD FPC, microSD push-push, speaker JST-SH 2-pin, Tag-Connect TC2030 (UART/EN/BOOT) for factory, tactile switches (side-actuated for PTT and camera).

## 8. Open items

- [ ] Final STT co-processor & its interface (UART vs USB host) — blocked by Phase 3 benchmark
- [ ] Display part number (2.4" ILI9341 vs 2.8" ST7789) → mechanical
- [ ] Charger part & thermal on 1 A charge
- [ ] Verify GPIO3/45/46 strap behaviour on prototype
