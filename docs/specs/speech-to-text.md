# Offline Speech-to-Text — Requirements, Trade Study & Benchmark Plan

Requirements: REQ-STT-001…006. Decision record: [ADR-0002](../adr/0002-offline-speech-recognition-architecture.md).

## 1. Why the ESP32-S3 alone is not enough

Espressif's ESP-SR **MultiNet** runs on the ESP32-S3 but recognises **up to 200 predefined commands** (English/Chinese), returning a command ID — it is not free-form dictation. General STT models (Whisper tiny = 39 M params, Vosk small ≈ 40–50 MB, streaming Zipformer ≈ 20–70 MB) need tens to hundreds of MB of RAM and far more compute than 8 MB PSRAM / 240 MHz LX7 provide at usable latency.

MultiNet is still useful for **voice commands** ("send", "cancel", "play") as a nice-to-have.

## 2. Candidate architectures

| # | Architecture | Engines to test | Pros | Cons |
|---|---|---|---|---|
| A | ESP32-S3 only + MultiNet | ESP-SR | Lowest power/BOM | ❌ Fails REQ-STT-001 (no dictation) |
| B | ESP32-P4 (dual RISC-V 400 MHz, 32 MB PSRAM) as co-processor | whisper.cpp tiny (quantised), Vosk-lite ports | Same vendor/toolchain | Unproven latency; likely ≫ real-time for Whisper |
| C | **Power-gated Linux SoM** (Cortex-A53/A7 class, ≥ 512 MB RAM) | sherpa-onnx streaming Zipformer, Vosk small, whisper.cpp tiny.en/base.en | Proven engines, multi-language, easy model swap | +300–600 mA while active, boot time, BOM/size |
| D | Linux SoM with NPU (e.g. RV1106-class) | sherpa-onnx / RKNN-converted models | Lower energy per utterance | Model conversion effort, toolchain risk |

**Working recommendation: C**, with D evaluated if energy is too high. ESP32-S3 stays the always-on controller and powers the SoM only for transcription (target ≤ 10 s on-time per utterance incl. boot from a minimal image — measure!).

Prototype hardware for Phase 3: Raspberry Pi Zero 2 W (A53 ×4, 512 MB) as the reference for C; one RV1106-class board for D.

## 3. Benchmark protocol (Phase 3 exit gate)

Test set: 100 utterances, 3–10 s each, 5 speakers (Indian-English accents + target regional language if required), recorded **through the actual MEMS mic at 16 kHz**, quiet and 60 dB babble noise.

| Metric | How | Pass |
|---|---|---|
| WER (quiet / noise) | jiwer vs. manual transcripts | ≤ 15 % / ≤ 30 % |
| Latency | end-of-speech → FINAL text, incl. cold boot | ≤ 2 × audio length, ≤ 12 s for 6 s clip |
| Energy per utterance | PPK2 on SoM rail | record; budget ≤ 5 mWh |
| RAM peak / model size | `/proc/<pid>/status`, file size | fits SoM with ≥ 30 % headroom |
| Boot to READY | GPIO timestamp | ≤ 6 s target |
| Languages | per engine | list |

Tooling: `tools/stt-bench/` (harness: corpus WER/CER, RTF, latency, peak RSS, optional energy; 60 prompts) and `tools/stt-coproc/` (the on-device service + UART protocol shared with `firmware/components/stt_link`).

Results table goes here and into `tests/reports/phase3-stt.md`.

| Engine / model | HW | WER quiet | WER noise | RTF | Cold latency 6 s clip | RAM | Energy/utt | Notes |
|---|---|---|---|---|---|---|---|---|
| _tbd_ | | | | | | | | |

## 4. UX for unclear speech (REQ-STT-003)

* Engine confidence < threshold → show "Didn't catch that — Retry / Send as voice / Edit".
* Always offer **send as voice note** as fallback (same recording).
* Editing: on-screen keyboard (T9-style on nav buttons) for small corrections.

## 5. Pre-processing (REQ-STT-005)

High-pass 100 Hz, ESP-SR AFE noise suppression (NS) on the ESP32 before streaming PCM to the SoM, AGC to −20 dBFS.
