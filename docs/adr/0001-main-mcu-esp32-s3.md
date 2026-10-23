# ADR-0001: ESP32-S3-WROOM-1-N16R8 as main controller

* Status: accepted (provisional until Phase 1 bring-up)
* Date: 2026-10-23
* Requirements: REQ-SYS-001, REQ-CAM-001, REQ-AUD-001, REQ-PCB-002

## Context
Need camera (DVP), I²S audio, SPI LoRa/LCD/SD, USB, low-power idle, AES, ≥ 8 MB RAM for JPEG + LVGL buffers.

## Options considered
ESP32-S3 (N16R8), ESP32-P4 (no radio needed but higher power, no low-power idle comparable), STM32H7 + external PSRAM (more effort, no mature camera/LVGL ecosystem parity).

## Decision
ESP32-S3-WROOM-1-N16R8: native DVP camera + esp32-camera driver, 2× I²S, USB-Serial-JTAG, octal PSRAM, hardware AES/SHA, mature ESP-IDF. Wi-Fi/BLE unused (disabled) — module still pre-certified which eases nothing for LoRa ETA but simplifies RF layout.

## Consequences
* GPIO budget is fully consumed (see pin map) → I²C expander mandatory.
* Free-form STT cannot run on it → ADR-0002.

## Validation
Phase 1/5 dev-kit bring-up with camera + LCD + LoRa sharing SPI.
