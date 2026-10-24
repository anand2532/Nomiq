# Hardware

KiCad 8+ project in `kicad/nomiq/`. Open `nomiq.kicad_pro`. Everything in it is **generated** from
Python sources in `design/` — edit those, regenerate, and let CI verify:

| Step | Source | Command | Check (make tools / CI) |
|---|---|---|---|
| Netlist + pin map (138 parts, 99 nets) | `design/nomiq_hw.py` | `python3 hardware/design/nomiq_hw.py` | `--check` (firmware pin map, straps, supplies) |
| Footprint library (36) | `design/fplib.py` | `python3 hardware/design/fplib.py` | `--check` (library up to date, courtyards) |
| Schematic (8 sheets) | `design/gen_kicad.py` | `python3 hardware/design/gen_kicad.py` | `--check` (connectivity = spec, pad maps) |
| PCB: placement + routing | `design/board.py`, `placement.py`, `gen_pcb.py` | `python3 hardware/design/gen_pcb.py` (~10 min) | `pcb_check.py` (clearance, edge, courtyards, connectivity) |
| BOM / CPL / cost | `design/gen_bom.py` | `python3 hardware/design/gen_bom.py` | — |
| Zone fill, KiCad DRC, Gerbers, drill, STEP | `kicad/tools/fab.py` | CI job `kicad` (kicad/kicad:8.0) | artifact `nomiq-a0-fab` |

PCB: 64.6 × 124.6 mm, 4 layers (F signal / In1 GND / In2 3V3 / B signal), JLC04161H-7628, 0.127 mm rule,
0.6/0.3 mm vias. **F side = electronics (faces the battery/back cover), B side = user interface (display
module, keys, speaker connector).** Mechanical interface (outline, bosses, keys, display, USB, mic, camera,
SMA) comes from `mechanical/enclosure/params.py`. Routing is produced by the project's grid router
(A* in `design/router/astar.c`); the RF section (SX1262 → PA filter / balun → PE4259 → u.FL) is routed
first on F.Cu without vias over the In1 ground plane and must be reviewed and tuned on a VNA.

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
