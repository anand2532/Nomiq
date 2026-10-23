# Phase 1–6 dev-kit wiring (ESP32-S3-DevKitC-1 N16R8 + SX1262 module)

Matches `firmware/components/board/include/board_pins.h` so the same firmware later runs on the PCB.
Select **Nomiq board → Dev kit** in `idf.py menuconfig` (default).

| SX1262 module pin (E22-900M22S / Core1262-HF) | ESP32-S3 GPIO | Note |
|---|---|---|
| VCC | 3V3 | module draws ~120 mA at +22 dBm — use the DevKit 3V3, not a GPIO |
| GND | GND | |
| SCK | 39 | shared SPI2 |
| MOSI | 40 | |
| MISO | 41 | |
| NSS | 42 | |
| BUSY | 2 | |
| DIO1 | 1 | IRQ |
| NRST | 14 | dev kit only (`CONFIG_NOMIQ_DEVKIT_LORA_NRST_GPIO`); PCB uses the IO expander |
| TXEN / RXEN (E22 only) | — | E22 has DIO2-controlled switch internally? If your module exposes TXEN/RXEN, tie RXEN = !TXEN via DIO2 or wire to spare GPIOs and disable `dio2_rf_switch` |
| ANT | 868 MHz antenna | **never transmit without an antenna** |

Later phases (same pins as the PCB): I²S mic/amp on 47/48/46/3, I²C on 4/5, camera DVP per board_pins.h, TFT CS/DC on 21/14 (move NRST to the expander-equivalent when the LCD is added).

## Phase 1 procedure

1. `idf.py set-target esp32s3 && idf.py menuconfig` → Nomiq application → leave peer = 0 on unit B, flash both.
2. Read each unit's `device id` from the boot log; set `NOMIQ_PEER_ID` on unit A to B's ID (and vice-versa for two-way), reflash.
3. `idf.py monitor | tee range_<location>.log` — collect `RANGE,...` CSV lines at each test distance.
4. Exit criterion (roadmap Phase 1): ≥ 99 % `delivered=1` over 1000 pings at 500 m LOS.
