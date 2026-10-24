# CLAUDE.md — Nomiq project instructions

Nomiq is a standalone LoRa voice / speech-to-text / photo messenger: ESP32-S3 + SX1262 (865–867 MHz India) + I²S audio + OV2640 + SPI TFT + microSD + 1S Li-ion, with an offline STT co-processor. No phone, internet or cloud — ever.

## Read first
- `docs/00-requirements.md` — REQ-IDs; cite them in commits, issues and PRs.
- `docs/01-system-architecture.md`, `docs/02-hardware-architecture.md` — blocks, pin map, buses, power tree.
- `docs/03-development-roadmap.md` — phases and gates.
- `docs/adr/` — decisions; don't contradict an accepted ADR without writing a new one.

## Engineer agents (`.claude/agents/`)
| Agent | Owns |
|---|---|
| project-manager | planning, gates, issues, cross-team coordination |
| system-architect | architecture docs, ADRs, pin map |
| electronics-schematic-engineer | KiCad schematic (kicad MCP) |
| pcb-layout-rf-engineer | KiCad PCB, RF, fab outputs (kicad MCP) |
| firmware-engineer | ESP-IDF firmware, UI, storage |
| lora-protocol-engineer | radio, air protocol, crypto |
| audio-speech-engineer | audio, Codec2, offline STT benchmark |
| camera-imaging-engineer | camera + image pipeline |
| power-systems-engineer | charger, battery, power budget |
| mechanical-enclosure-engineer | enclosure (build123d / FreeCAD MCP) |
| test-validation-engineer | test plans, reports, bring-up |
| bom-sourcing-engineer | BOM, pricing, availability |
| git-ops-bot | ALL git/GitHub ops: issue → branch → PR → merge as anand2532 |

Delegate to the owning agent; for cross-cutting work start with project-manager.

## MCP servers (`.mcp.json`) — see `docs/ai-workflow.md` for setup
- `kicad` → kicad-mcp-pro (schematic/PCB inspection, ERC/DRC, DFM, BOM, exports)
- `build123d` → build123d-mcp (parametric CAD, render, measure, STEP/STL)
- `freecad` → freecad-mcp (optional, needs FreeCAD + its addon running)

## Invariants
1. Pin map lives in two places that must change together: `docs/02-hardware-architecture.md` and `firmware/components/board/include/board_pins.h`.
2. Protocol spec `docs/specs/communication-protocol.md` and `firmware/components/nomiq_proto/` change together; host tests must pass: `make -C firmware/components/nomiq_proto/test`.
3. Enclosure dimensions only in `mechanical/enclosure/params.py`; mechanical interfaces change together with the PCB.
4. Radio: 125 kHz bandwidth only (India ≤ 200 kHz). Regulatory profiles per ADR-0003.
5. No cloud/network dependency in firmware or STT.
6. Hardware PRs attach ERC/DRC results (`/hw-check`).

## Commands
- `python3 tools/lora_airtime.py --table` — transfer-time table
- `make test` — all host tests (protocol, transfer simulation, crypto vectors, message store, tools)
- `python3 mechanical/enclosure/enclosure.py --out mechanical/exports` — build enclosure
- `cd firmware && idf.py set-target esp32s3 build` — firmware build (ESP-IDF ≥ 5.3)
- `python3 hardware/design/gen_kicad.py` / `fplib.py` / `gen_pcb.py` / `gen_bom.py` — regenerate schematic, footprints, PCB, BOM
- `python3 hardware/design/pcb_check.py` — independent DRC + connectivity check of the PCB file

## Git (via git-ops-bot / `tools/gitbot/gitflow.sh`)
Never push to main directly. Flow: `gitflow.sh start "<title>" <area> <phase>` → `gitflow.sh commit "type(scope): msg"` → `gitflow.sh pr` → CI → `gitflow.sh merge`. `gh` must be logged in as anand2532.
Branches `<type>/<issue#>-<slug>` (created by the bot). Work prepared offline is delivered as a bundle and published with `tools/gitbot/publish_queue.sh` (same issue → PR → merge flow); conventional commits (`feat(proto): …`, `fix(pcb): …`); tags `hw-vX.Y`, `fw-vX.Y.Z`, `mech-vX.Y`.
