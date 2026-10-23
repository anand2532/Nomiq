# Board A0 connection specification (code)

`nomiq_hw.py` is the machine-checked description of every part and every connection on the Nomiq main board, at the level of **datasheet pin names**. The KiCad schematic is captured from it (`out/connections.md`), and CI fails if it drifts from the firmware pin map.

```bash
python3 hardware/design/nomiq_hw.py          # checks + regenerate out/connections.md, out/bom.csv, out/nets.json
python3 hardware/design/nomiq_hw.py --check  # checks only (part of `make test`)
```

What the checker guarantees: every IC pin is connected or explicitly NC; no pin on two nets; no single-member nets; every supply pin on a power net; ESP32 GPIO wiring == `board_pins.h`; TCA9535 bit map == `IOX_*`; strap pins GPIO0/45/46 have their reset-safe resistors. (Mutation-tested: breaking any of these produces an error.)

What it does **not** guarantee: datasheet values. 29 parts are flagged ⚠️ *verify* (charger programming resistors, buck-boost divider, SX1262 RF/DC-DC network, TCXO coupling, amp mode resistor, FPC pinouts) — the schematic engineer checks each against the current datasheet / Semtech reference design while capturing, then clears the flag here.

## Workflow
1. Change the design **here** first (small PR, `/hw-check` style review).
2. Capture/update the KiCad sheets from `out/connections.md` (kicad MCP: `electronics-schematic-engineer`).
3. Run ERC; compare KiCad's netlist against `out/nets.json` (net names are normative).
