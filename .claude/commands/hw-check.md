---
description: Run KiCad ERC + DRC on the Nomiq board and summarise violations
---
Run, from the repo root:
1. `kicad-cli sch erc --severity-all --format json -o /tmp/erc.json hardware/kicad/nomiq/nomiq.kicad_sch`
2. `kicad-cli pcb drc --severity-all --schematic-parity --format json -o /tmp/drc.json hardware/kicad/nomiq/nomiq.kicad_pcb`
(or the equivalent kicad MCP validation tools). Summarise errors/warnings grouped by type and sheet, propose fixes, and flag any unconnected nets. Do not modify files unless asked. $ARGUMENTS
