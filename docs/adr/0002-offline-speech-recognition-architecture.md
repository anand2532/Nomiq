# ADR-0002: Offline STT on a power-gated co-processor

* Status: **proposed** — final after Phase 3 benchmark
* Date: 2026-10-23
* Requirements: REQ-SYS-003, REQ-SYS-008, REQ-STT-001…006

## Context
ESP-SR MultiNet on ESP32-S3 supports ≤ 200 fixed commands, not dictation. Brief requires general offline STT and allows a more capable processor while staying standalone.

## Options considered
See docs/specs/speech-to-text.md §2 (A: S3 only, B: ESP32-P4, C: Linux SoM, D: Linux SoM + NPU).

## Decision (provisional)
Option C: ESP32-S3 always-on controller + a Linux SoM on the same PCB, powered only during transcription via load switch (STT_EN), UART RPC (stt_link.h). Engines benchmarked: sherpa-onnx streaming Zipformer, Vosk small, whisper.cpp tiny.en/base.en. Option D benchmarked in parallel.

## Consequences
* PCB must reserve area/height for the SoM → enclosure z-height and battery choice affected.
* +BOM cost; +peak current (≥ 2 A VSYS design).
* Phase 7a blocked until Phase 3 gate.

## Validation
Phase 3 benchmark table meets pass criteria (WER, latency, energy).
