# KiCad project — Nomiq main board

Open `nomiq.kicad_pro` in KiCad 8 or newer. The schematic is **generated** from the netlist source of truth
`hardware/design/nomiq_hw.py` by `hardware/design/gen_kicad.py`:

```
python3 hardware/design/gen_kicad.py          # regenerate nomiq.kicad_sch, 01..08 sheets, lib/nomiq.kicad_sym
python3 hardware/design/gen_kicad.py --check  # rebuild connectivity from the generated S-expressions and compare with nomiq_hw.py (runs in make tools / CI)
kicad-cli sch erc nomiq.kicad_sch             # ERC on a machine with KiCad
```

Structure: root sheet with 8 hierarchical sheets (power, MCU, radio, audio, camera, display+storage, UI, STT).
Every pin is connected by a short wire to a global label carrying the net name, so connectivity is exact and
the sheets are readable; unused pins carry no-connect flags. Edit `nomiq_hw.py`, not the `.kicad_sch` files.

Pad numbers: U1 (ESP32-S3-WROOM-1) and U2 (SX1262 QFN-24) follow the datasheets. 32 parts still use
placeholder pad numbers (marked `unverified` in `gen_kicad.py`) — verify against datasheets and assign
footprints before PCB layout (electronics-schematic-engineer, gate G2).

- Project libraries: `lib/nomiq.kicad_sym` (generated), `lib/nomiq.pretty` (registered in sym-lib-table / fp-lib-table)
- PCB: create `nomiq.kicad_pcb` from the PCB editor (Update PCB from schematic); outline from `mechanical/enclosure/params.py`
- Fabrication outputs: `fab/` (Gerbers, drill, CPL, BOM) — release via tag `hw-vX.Y`
- MCP: `.mcp.json` points kicad-mcp-pro at this folder (`KICAD_MCP_PROJECT_DIR`)
