# 00 — Requirements (traceable)

Source: product brief (`project.txt` in the Claude project). Every requirement has an ID so tests, ADRs and issues can reference it. **Priority:** M = must (non-negotiable), S = should, C = could.
**Verify:** T = test, A = analysis, I = inspection, D = demonstration.

## 1. System / non-negotiable

| ID | Requirement | Pri | Verify |
|---|---|---|---|
| REQ-SYS-001 | Device is fully standalone: no smartphone, PC, internet, cellular or cloud for any normal operation | M | D |
| REQ-SYS-002 | Two or more identical devices communicate directly over LoRa (no gateway) | M | T |
| REQ-SYS-003 | Speech-to-text runs entirely on-device | M | T |
| REQ-SYS-004 | Text, voice and photos transferable device-to-device over LoRa | M | T |
| REQ-SYS-005 | Received messages accessible via the device's own display and speaker | M | D |
| REQ-SYS-006 | All messages stored locally | M | T |
| REQ-SYS-007 | System tolerates unreliable links and interrupted transfers | M | T |
| REQ-SYS-008 | Hardware selection validated against STT memory/compute needs before PCB freeze | M | A |

## 2. Radio & protocol

| ID | Requirement | Pri | Verify |
|---|---|---|---|
| REQ-RF-001 | Semtech SX1262 over SPI | M | I |
| REQ-RF-002 | Legal operating band for India (865–867 MHz, ≤200 kHz carrier BW) — see [link budget](specs/lora-link-budget.md) | M | A |
| REQ-RF-003 | Unique device ID, destination addressing | M | T |
| REQ-RF-004 | ACKs and retransmissions | M | T |
| REQ-RF-005 | Fragmentation and reassembly | M | T |
| REQ-RF-006 | Duplicate packet detection | M | T |
| REQ-RF-007 | Progress and timeout handling | M | T |
| REQ-RF-008 | Interrupted-transfer recovery / resume | M | T |
| REQ-RF-009 | End-to-end encryption and device authentication | M | T |
| REQ-RF-010 | Text prioritised over voice and photos | M | T |
| REQ-RF-011 | Packet fields: version, src, dst, msg ID, type, seq, frag idx/count, length, integrity, auth | M | I |
| REQ-RF-012 | Message cancellation | S | T |
| REQ-RF-013 | Estimated transfer time shown before/while sending | S | D |

## 3. Audio & speech

| ID | Requirement | Pri | Verify |
|---|---|---|---|
| REQ-AUD-001 | I²S digital MEMS microphone | M | I |
| REQ-AUD-002 | Speaker with amplifier | M | I |
| REQ-AUD-003 | Push-and-hold record button | M | D |
| REQ-AUD-004 | Low-bitrate codec for voice transfer | M | T |
| REQ-AUD-005 | Configurable max recording duration | M | T |
| REQ-AUD-006 | Playback controls (play/pause/replay) | M | D |
| REQ-STT-001 | Offline STT, no cloud, no phone | M | T |
| REQ-STT-002 | Text preview, accept / edit / discard before send | M | D |
| REQ-STT-003 | Graceful handling of unclear speech | M | T |
| REQ-STT-004 | Configurable language where hardware permits | S | T |
| REQ-STT-005 | Mic noise reduction where practical | S | T |
| REQ-STT-006 | Benchmarked: flash, RAM, accuracy (WER), latency, power, model load time | M | A |

## 4. Camera

| ID | Requirement | Pri | Verify |
|---|---|---|---|
| REQ-CAM-001 | OV2640 or compatible, dedicated capture button | M | I |
| REQ-CAM-002 | JPEG with configurable resolution and quality | M | T |
| REQ-CAM-003 | Preview, accept / retake | M | D |
| REQ-CAM-004 | Configurable max file size; auto down-scale to fit | M | T |
| REQ-CAM-005 | Receiver validates image integrity before display | M | T |

## 5. UI

| ID | Requirement | Pri | Verify |
|---|---|---|---|
| REQ-UI-001 | TFT display (photos need more than OLED) | M | I |
| REQ-UI-002 | Nav (buttons or rotary encoder), Select, Back, Record, Camera, Power buttons | M | I |
| REQ-UI-003 | Shows: own ID, recipient, inbox, rec status, STT preview, photo preview, TX progress, RSSI/SNR, battery, failures/retries | M | D |
| REQ-UI-004 | Communicates long transfer times to the user | M | D |

## 6. Storage

| ID | Requirement | Pri | Verify |
|---|---|---|---|
| REQ-STO-001 | microSD (or equivalent) for audio, photos, text, metadata | M | I |
| REQ-STO-002 | Message history browsable | M | D |
| REQ-STO-003 | Pending messages persist until delivered or cancelled (survive reboot) | M | T |
| REQ-STO-004 | Storage-full handled gracefully | M | T |

## 7. Power

| ID | Requirement | Pri | Verify |
|---|---|---|---|
| REQ-PWR-001 | 1S Li-ion/LiPo, initial 3000 mAh | M | I |
| REQ-PWR-002 | USB-C charging | M | T |
| REQ-PWR-003 | Battery protection (OV/UV/OC/short, thermal) | M | T |
| REQ-PWR-004 | Battery voltage / SoC monitoring, low-battery warning | M | T |
| REQ-PWR-005 | Display timeout, low-power idle, per-subsystem power gating | M | T |

## 8. PCB

| ID | Requirement | Pri | Verify |
|---|---|---|---|
| REQ-PCB-001 | KiCad, 4-layer preferred | S | I |
| REQ-PCB-002 | USB-C programming and charging | M | T |
| REQ-PCB-003 | RF connector (u.FL/SMA) and RF layout per Semtech reference | M | I |
| REQ-PCB-004 | Debug and manufacturing test points | M | I |
| REQ-PCB-005 | ESD protection on all external interfaces | M | I |
| REQ-PCB-006 | RF separated from switching regulators / digital noise | M | I |
| REQ-PCB-007 | ERC and DRC clean, no unintended unconnected nets | M | A |

## 9. Mechanical

| ID | Requirement | Pri | Verify |
|---|---|---|---|
| REQ-MEC-001 | Handheld, ~130×70×30 mm (preliminary) | S | I |
| REQ-MEC-002 | Openings: camera, mic, speaker grille, display window, buttons, USB-C | M | I |
| REQ-MEC-003 | Protected antenna mounting or accessible connector | M | I |
| REQ-MEC-004 | Secure, preferably replaceable battery | M | I |
| REQ-MEC-005 | 3D-printable prototype | M | D |

## 10. Deliverables (brief §11)

System architecture · BOM · schematic · PCB + fab files · firmware · STT integration · audio · camera & image transfer · protocol docs · 3D enclosure · bring-up instructions · test report (radio, WER, transfer times, battery life).
