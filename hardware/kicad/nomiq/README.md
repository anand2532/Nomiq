# KiCad project — Nomiq main board

Open `nomiq.kicad_pro` in KiCad 9 or 10. The schematic is an empty root sheet; capture follows the sheet plan in `../../README.md`. Create `nomiq.kicad_pcb` from the PCB editor on first open (board outline from `mechanical/enclosure/params.py`).

- Project libraries: `lib/nomiq.kicad_sym`, `lib/nomiq.pretty` (registered in sym-lib-table / fp-lib-table)
- Fabrication outputs: `fab/` (Gerbers, drill, CPL, BOM) — release via tag `hw-vX.Y`
- MCP: `.mcp.json` points kicad-mcp-pro at this folder (`KICAD_MCP_PROJECT_DIR`)
