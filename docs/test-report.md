# Nomiq — Test Report (deliverable 12)

Status: **A0 pre-hardware.** Columns marked *predicted* come from analysis, host simulation and
the link/power budgets in this repo; columns marked *measured* are filled in during bring-up
(`hardware/bringup/bringup-checklist.md`) on two dev-kit prototypes (Phase 1) and two A0 boards
(Phase 7). Keep raw logs in `test/logs/<date>-<test>/` and reference them here.

| Area | Requirement | Predicted / simulated | Measured | Pass |
|---|---|---|---|---|
| Radio range, urban NLOS | usable link | 1–3 km @ SF9/125, +22 dBm (link budget) | | |
| Radio range, LOS | usable link | 5–10 km | | |
| Text latency | immediate, ACKed | 1.3 s @ SF9 (140 B) | | |
| Voice 15 s (Codec2 1200) | minutes at most | 14.7 s @ SF9 | | |
| Photo 320x240 Q25 (12 kB) | progress + ETA shown | 76.5 s @ SF9 | | |
| Transfer at 40 % frame loss | completes, resumes | completes (host sim, 3 devices) | | |
| STT word error rate (EN, quiet) | usable | target ≤ 15 % WER (ADR-0002 gate) | | |
| STT latency (10 s utterance) | interactive | target ≤ 3 s on SoM | | |
| Battery life, typical day | ≥ 3 days | ≈ 13 days (186 mAh/day, 2550 mAh usable) | | |
| Idle current (RX duty-cycled) | low-power idle | 2–3 mA | | |

## 1. Radio performance

**Setup.** Two devices, firmware `NOMIQ_APP_LINK_TEST`, SF7–SF12 at 125 kHz, +22 dBm (IN865_2008)
and 14 dBm / 2.5 % duty (IN865_SRD2021), 2 dBi whip on the SMA. Log per packet: RSSI, SNR,
frame-counter, CRC/MIC failures. Sites: indoor (same floor, 3 floors), urban street (0.5, 1, 2, 3 km),
open LOS (2, 5, 10 km).

| Site / distance | SF | PER % | RSSI dBm | SNR dB | Notes |
|---|---|---|---|---|---|
| | | | | | |

Conducted checks on A0 (VNA / SA): antenna port return loss (target ≤ −10 dB at 865–867 MHz after
tuning C46/C48), TX power at the u.FL (+22 dBm ± 1 dB), 2nd/3rd harmonic (PA low-pass L4/C37/L5/C38),
TCXO frequency error at 25 °C (≤ ±2 ppm ≈ ±1.7 kHz), occupied bandwidth ≤ 200 kHz.

## 2. Transfer times

Predicted by `python3 tools/lora_airtime.py --table` (CR4/5, 8-symbol preamble, 10 % PER, 223-byte
fragments, no duty limit; ×40 TX airtime under a 2.5 % duty profile):

| SF/125 kHz | text 140 B | voice 15 s | voice 30 s | photo 4 kB | photo 12 kB | photo 20 kB |
|---|---|---|---|---|---|---|
| SF7 | 0.5 s | 4.7 s | 9.3 s | 8.3 s | 24.5 s | 40.7 s |
| SF8 | 0.8 s | 8.2 s | 16.3 s | 14.4 s | 42.9 s | 71.3 s |
| SF9 | 1.3 s | 14.7 s | 29.1 s | 25.7 s | 76.5 s | 127.3 s |
| SF10 | 2.3 s | 26.4 s | 52.3 s | 46.3 s | 137.6 s | 228.9 s |
| SF11 | 5.1 s | 58.2 s | 115.6 s | 102.2 s | 304.4 s | 506.6 s |
| SF12 | 9.2 s | 105.1 s | 208.3 s | 184.4 s | 548.8 s | 913.4 s |

Host simulation (`make test`, transfer engine + 3-device messenger simulation) covers windowed
SACK, duplicate filtering, priority (text overtakes a running photo), resume after power loss
(sha8 + re-OFFER) and up to 40 % random frame loss. **Measure** on hardware: wall-clock from SEND to
"delivered" for each row above at the SF chosen by the adaptive rate, and with the transfer
interrupted (receiver power-cycled at 50 %).

| Message | SF used | Predicted | Measured | Resumed OK |
|---|---|---|---|---|
| | | | | |

## 3. Transcription accuracy

Method: `tools/stt-bench` (`bench.py` + `wer.py`) with `prompts_en.txt` read by ≥ 3 speakers in
quiet room, office noise (~55 dB(A)) and street (~70 dB(A)), recorded through the device microphone
path (ICS-43434, 16 kHz) and transcribed on the STT co-processor (ADR-0002 candidates). Report WER,
real-time factor, peak RAM, model size and SoM energy per utterance.

| Engine / model | Condition | WER % | RTF | Latency 10 s | RAM MB | Energy mJ |
|---|---|---|---|---|---|---|
| | | | | | | |

Acceptance (Phase 3 gate): WER ≤ 15 % quiet / ≤ 25 % office, latency ≤ 3 s for 10 s audio, fits SoM
RAM with the UART service; otherwise change engine or SoM before PCB A1.

## 4. Battery life

Method: Nordic PPK2 (or INA219 logger) in series with the battery at 3.7 V; profile each state of
`docs/specs/power-budget.md`, then a scripted "typical day" (10 voice notes, 10 STT messages,
5 photos, 20 received items, 25 min screen) and a standby run to 20 % charge.

| State | Predicted mA | Measured mA |
|---|---|---|
| Deep idle, RX duty-cycled | 2–3 | |
| Screen on, browsing | 90 | |
| Recording | 110 | |
| Playback (avg) | 250 | |
| Camera preview | 240 | |
| LoRa TX +22 dBm | 180 | |
| STT active | 540 | |

| Scenario | Predicted | Measured |
|---|---|---|
| Typical day | ≈ 186 mAh/day → ~13 days | |
| Standby (idle listen only) | ≈ 2.5 mA → ~40 days | |
| Charge 0→100 % (1.0 A) | ≈ 3.5 h | |

## 5. Board-level verification (A0)

| Check | Tool | Result |
|---|---|---|
| Schematic connectivity = spec | `gen_kicad.py --check` | pass (CI) |
| Footprints / pad maps | `fplib.py --check`, `gen_kicad.py` | pass (CI) |
| ERC | `kicad-cli sch erc` (CI job kicad) | see CI artifact |
| PCB clearance + connectivity | `pcb_check.py` | see `hardware/design/out/pcb_route_report.txt` |
| KiCad DRC after zone fill | CI job kicad (`fab.py`) | see CI artifact |
| Enclosure fit (PCB, display, battery, USB) | `enclosure.py` interference check | see CI log |
| Power-on, rails, current limit | bring-up checklist §2 | |
| USB enumeration / flashing | bring-up checklist §3 | |
