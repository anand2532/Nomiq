# Board Bring-up Checklist (per board serial)

| # | Step | Expected | Result |
|---|---|---|---|
| 1 | Visual + continuity: VBUS/VSYS/3V3/GND not shorted | > 1 kΩ | |
| 2 | Current-limited (100 mA) USB power, no battery | VSYS ≈ 4.2–4.4 V, 3V3 = 3.30 ± 0.05 V | |
| 3 | Battery only | 3V3 regulated down to 3.0 V batt | |
| 4 | Flash via USB-C (esptool) | Chip ID, 16 MB flash, 8 MB PSRAM detected | |
| 5 | I²C scan | 0x20 expander, 0x36 fuel gauge, 0x30 OV2640, charger | |
| 6 | Buttons via expander | All 6 + power sense | |
| 7 | LCD init + backlight PWM | Test pattern | |
| 8 | microSD mount, 10 MB write/read | CRC match | |
| 9 | SX1262 GetStatus / version | BUSY toggles, standby OK | |
| 10 | CW at 866 MHz, +14 dBm then +22 dBm (into attenuator + SA) | ±2 ppm, power ± 1 dB | |
| 11 | Two-board ping (Phase 1 firmware) | RSSI/SNR sane | |
| 12 | Mic capture 1 kHz tone | No clipping, noise floor logged | |
| 13 | Amp playback | Clean, no brown-out | |
| 14 | Camera JPEG 320×240 | Image OK | |
| 15 | STT co-proc power on → READY over UART | Boot time logged | |
| 16 | Charge 0.5 → 1 A, thermal | < 45 °C on charger | |
| 17 | Sleep current (screen off, RX duty-cycled) | ≤ 3 mA | |
