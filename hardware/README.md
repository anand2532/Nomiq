# Hardware

KiCad 9/10 project in `kicad/nomiq/`. Open `nomiq.kicad_pro`.

**Capture from `design/out/connections.md`** — generated and checked by `design/nomiq_hw.py` (128 parts, 97 nets, consistency with the firmware pin map enforced in CI).

## Schematic sheet plan (hierarchical)

| Sheet | Contents |
|---|---|
| 01_power | USB-C, ESD, charger/power-path, battery + protection, fuel gauge, 3V3 buck-boost, load switches |
| 02_mcu | ESP32-S3 module, boot/reset, USB, strapping, I²C expander, Tag-Connect |
| 03_radio | SX1262, TCXO, matching network, RF switch, u.FL, shield can |
| 04_audio | MEMS mic, class-D amp, speaker connector |
| 05_camera | OV2640 FPC, LDOs, power gating |
| 06_display_storage | LCD FPC + backlight driver, microSD |
| 07_ui | Buttons, LED |
| 08_stt | STT co-processor module + interface (after ADR-0002) |

## Rules

* Library: project-local `lib/nomiq.kicad_sym`, `lib/nomiq.pretty`; every symbol has MPN, manufacturer, LCSC fields.
* Net names follow `docs/02-hardware-architecture.md` (e.g. `LORA_NSS`, `I2S_BCLK`).
* Before any PR: `kicad-cli sch erc` and `kicad-cli pcb drc` (or the `/hw-check` command) — zero errors; warnings justified in PR.
* Fab target: JLCPCB 4-layer, 1.6 mm, JLC04161H-7628 stack-up, min 0.1 mm track/space, 0.2 mm via.

## Design review checklist (gate for 7a/7b)

- [ ] Strap pins GPIO0/3/45/46 safe at reset
- [ ] ESD on USB, buttons, SD, antenna
- [ ] SX1262 layout per Semtech ref; 50 Ω CPWG verified with stack-up calculator
- [ ] RF section ≥ 10 mm from buck-boost inductor; ground stitching
- [ ] Decoupling per datasheets, placed at pins
- [ ] Battery NTC, charge current set, thermal relief
- [ ] Test points: VBUS, VSYS, 3V3, VBAT, GND, UART, EN, BOOT, SPI, I²S
- [ ] Mounting holes / keep-outs match `mechanical/enclosure/params.py`
- [ ] PCB STEP exported to `mechanical/exports/pcb.step`
