# ADR-0004: Codec2 for voice messages

* Status: accepted (provisional)
* Date: 2026-10-23
* Requirements: REQ-AUD-004

## Context
At SF9/125 effective goodput is ~1.3 kbit/s. Opus at its 6 kbit/s floor would make 30 s ≈ 22 kB (≈ 2.5 min at SF9).

## Decision
Codec2 (open source, LGPL) 1200 bps default (30 s → 4.5 kB → ~28 s at SF9), 2400/3200 for better quality on strong links, 700C for weak links. Raw 16 kHz PCM kept locally for STT and local replay.

## Consequences
Robotic but intelligible voice; LGPL-2.1 — on a statically linked MCU image we must provide relinkable object files; review licensing before product.

## Validation
Phase 4 MOS-style listening test (5 listeners) + CPU load on ESP32-S3 (< 30 % one core).
