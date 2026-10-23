---
description: Estimate LoRa transfer time for a payload (e.g. /airtime 12000 sf9)
---
Parse "$ARGUMENTS" as `<bytes> [sfN] [duty%]` and run `python3 tools/lora_airtime.py --bytes <bytes> --sf <N> --per 0.1 [--duty <d>]`. Also show the full table (`--table`) if no arguments. Explain the result against the India 125 kHz / duty-cycle profiles in docs/specs/lora-link-budget.md.
