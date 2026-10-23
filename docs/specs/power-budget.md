# Power Budget (estimate — replace with PPK2 measurements in Phase 6)

Battery: 3000 mAh × 3.7 V ≈ 11.1 Wh, usable ≈ 85 % → **9.4 Wh**. Currents at 3.7 V battery side, incl. ~88 % converter efficiency.

| State | ESP32-S3 | SX1262 | LCD+BL | Camera | Amp | STT SoM | Total |
|---|---|---|---|---|---|---|---|
| Deep idle, RX duty-cycled (CAD) | 0.3 mA (light sleep) | 1.5 mA avg | 0 | 0 | 0 | 0 | **≈ 2–3 mA** |
| Screen on, browsing | 40 mA | 5 mA (RX) | 45 mA | 0 | 0 | 0 | ≈ 90 mA |
| Recording | 60 mA | 5 | 45 | 0 | 0 | 0 | ≈ 110 mA |
| Playback (avg) | 50 | 5 | 45 | 0 | 150 | 0 | ≈ 250 mA |
| Camera preview | 90 | 5 | 45 | 100 | 0 | 0 | ≈ 240 mA |
| LoRa TX +22 dBm | 40 | 120 | 20 (dim) | 0 | 0 | 0 | ≈ 180 mA |
| STT active | 40 | 5 | 45 | 0 | 0 | 450 | ≈ 540 mA |

## Typical day (target ≥ 3 days)

| Activity | Duration/day | mAh |
|---|---|---|
| Idle listen | 23.3 h | 23.3 h × 2.5 mA = 58 |
| Screen use | 25 min | 38 |
| 10 voice notes (record + TX ≈ 40 s each) | 7 min | 21 |
| 10 STT messages (≈ 12 s SoM each) | 2 min | 18 |
| 5 photos (preview + 75 s TX) | 8 min | 26 |
| 20 received items played/viewed | 10 min | 25 |
| **Total** | | **≈ 186 mAh/day → ~13 days on 2550 mAh usable** |

Comfortably meets ≥ 3 days; the dominant risk is **idle RX current** — must use SX1262 RX duty-cycle / CAD mode with a preamble long enough to wake (trade-off: longer preamble → more airtime per packet). Verify in Phase 1.

Peak current: camera + TX + amp must not overlap (firmware interlock) → design 3V3 rail for ≥ 1.2 A, VSYS for ≥ 2 A peak (SoM boot spike).
