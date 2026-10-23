# Nomiq — Standalone LoRa Voice, Text & Photo Messenger

Nomiq is a pocket-sized, battery-powered messenger that sends **voice notes, offline speech-to-text messages and photographs** directly between devices over **LoRa** — no phone, no internet, no cellular, no cloud.

| | |
|---|---|
| **Main MCU** | ESP32-S3 (16 MB flash / 8 MB PSRAM) |
| **Radio** | Semtech SX1262, 865–867 MHz (India), 125 kHz BW |
| **Speech-to-text** | Offline — power-gated Linux co-processor (see [ADR-0002](docs/adr/0002-offline-speech-recognition-architecture.md)) |
| **Audio** | I²S MEMS mic, I²S class-D amp + speaker, Codec2 1200/2400 bps |
| **Camera** | OV2640 (DVP), on-chip JPEG |
| **Display** | 2.4–2.8" SPI TFT, 320×240 |
| **Storage** | microSD |
| **Power** | 1S Li-ion 3000 mAh, USB-C charging, fuel gauge |
| **Enclosure** | ~130 × 70 × 30 mm, 3D-printed (build123d, parametric) |
| **EDA** | KiCad 9/10, 4-layer |

## Repository map

```
docs/                    Requirements, architecture, specs, ADRs, roadmap
  00-requirements.md       Traceable requirements (REQ-xxx) from the product brief
  01-system-architecture.md
  02-hardware-architecture.md   Block diagram, pin map, power tree
  03-development-roadmap.md     Phases, gates, deliverables
  specs/                   Protocol, link budget, power budget, STT, UI, storage
  adr/                     Architecture Decision Records
hardware/
  kicad/nomiq/             KiCad project (schematic + PCB)
  bom/                     BOM and component selection
  bringup/                 Board bring-up checklist
mechanical/
  enclosure/               Parametric enclosure (build123d)
  exports/                 Generated STEP / STL (CI artefacts)
firmware/                  ESP-IDF project; protocol core is host-testable C
tools/                     Calculators (LoRa airtime, power budget)
tests/                     Test plans and reports
.claude/agents/            Engineering agent "bots" (one per discipline)
.mcp.json                  MCP servers: KiCad + CAD
```

## Getting started

```bash
# LoRa airtime / transfer-time table (India-compliant presets)
python3 tools/lora_airtime.py --table

# Host unit tests for the protocol core (no hardware needed)
make -C firmware/components/nomiq_proto/test

# Build the enclosure (needs: pip install build123d)
python3 mechanical/enclosure/enclosure.py --out mechanical/exports
```

Working with Claude Code and the engineering agents: see [docs/ai-workflow.md](docs/ai-workflow.md).

## Status

Phase 0 — architecture & de-risking. Original brief: [project.txt](project.txt). See [docs/03-development-roadmap.md](docs/03-development-roadmap.md).

**Gating risks (must close before PCB freeze):**
1. Offline STT on target hardware (Phase 3 benchmark) — ESP32-S3 alone cannot do free-form dictation.
2. Indian regulatory profile (duty-cycle / ERP) — drives photo/voice transfer times. See [lora-link-budget](docs/specs/lora-link-budget.md).
3. ESP32-S3 GPIO budget with camera + display + LoRa + SD + I²S — see pin map.
