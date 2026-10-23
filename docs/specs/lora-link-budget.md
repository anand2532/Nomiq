# LoRa Regulatory Profile, Link Budget & Transfer Times

## 1. Indian regulatory situation (verify before product release)

Two notifications are relevant; **confirm with WPC / an accredited test lab which applies to Nomiq** before ETA (Equipment Type Approval) application:

| Notification | Band | Limits | Duty cycle |
|---|---|---|---|
| G.S.R. 564(E), 30 Jul 2008 (DoT clarification 4 Jul 2018) | 865–867 MHz | ≤ 1 W TX power, ≤ 4 W ERP, **≤ 200 kHz carrier BW** | not specified |
| G.S.R. 853(E), 10 Dec 2021 — SRD exemption rules, 865–868 MHz | 865–868 MHz | Tracking/data-acquisition class: ≤ 500 mW ERP, ≤ 200 kHz, **APC required** | **≤ 2.5 %** (non-access-point) |

Consequences for design:

* **Bandwidth fixed at 125 kHz** (250/500 kHz exceed 200 kHz). This caps raw throughput at ~5.5 kbit/s (SF7).
* SX1262 max +22 dBm (158 mW) conducted + ~2 dBi antenna ≈ 250 mW ERP → within both regimes.
* Firmware implements **two regulatory profiles**, selected at build/config time:
  * `IN865_2008` — no duty-cycle limiter, 865.0625–866.9375 MHz channels.
  * `IN865_SRD2021` — 2.5 % rolling-hour duty-cycle limiter + adaptive TX power.
* Under a 2.5 % limit a 12 kB photo takes **~15 min at SF7** and **~49 min at SF9** (vs 24 s / 75 s unconstrained). The UI must show this ETA (REQ-RF-013, REQ-UI-004).
* ETA is required for any wireless product sold in India regardless of exemption.

## 2. Channel plan (proposal)

8 channels × 125 kHz in 865.0–867.0 MHz: 865.0625, 865.4025, 865.9850, 866.2, 866.4, 866.6, 866.8 (+ one control). One **rendezvous/control channel** for OFFER/ACK/text; bulk DATA may hop to a less busy channel announced in ACCEPT.

## 3. Link budget (SF9 / 125 kHz)

| Item | Value |
|---|---|
| TX power | +22 dBm |
| TX/RX antenna gain (stubby whip) | +2 / +2 dBi |
| Cable + connector + body loss | −3 dB each side |
| RX sensitivity SX1262 @ SF9/125 | −129 dBm |
| **Max path loss** | **≈ 149 dB** |
| → Free space | ≫ 10 km; practical urban NLOS 1–3 km, LOS 5–10 km |

## 4. Transfer-time table

Generated with `python3 tools/lora_airtime.py --table` (CR4/5, 10 % PER, 223 B payload/fragment, encrypted frames, windowed SACK, no duty limit):

| SF/125 kHz | ToA 255 B | Text 140 B | Voice 15 s (2.3 kB) | Voice 30 s (4.5 kB) | Photo 4 kB | Photo 12 kB | Photo 20 kB |
|---|---|---|---|---|---|---|---|
| SF7 | 394 ms | 0.5 s | 4.7 s | 9.3 s | 8.3 s | 25 s | 41 s |
| SF8 | 697 ms | 0.8 s | 8.2 s | 16 s | 14 s | 43 s | 71 s |
| SF9 | 1250 ms | 1.3 s | 15 s | 29 s | 26 s | 77 s | 127 s |
| SF10 | 2255 ms | 2.3 s | 26 s | 52 s | 46 s | 138 s | 229 s |
| SF11 | 5001 ms | 5.1 s | 58 s | 116 s | 102 s | 304 s | 507 s |
| SF12 | 9019 ms | 9.2 s | 105 s | 208 s | 184 s | 549 s | 913 s |

## 5. Adaptive data rate (peer-to-peer)

* Control frames (OFFER/ACK) at the link's current "safe" SF (default SF9).
* Receiver reports SNR in every ACK; sender picks the lowest SF with ≥ 5 dB margin over the SF's demod floor (SF7 −7.5 dB … SF12 −20 dB).
* Defaults: text SF9; bulk SF7–SF9 by margin; fall back one SF after 3 consecutive window failures.

## 6. Payload sizing defaults

| Type | Default | Config range |
|---|---|---|
| Voice codec | Codec2 1200 bps (150 B/s) | 700C / 1200 / 2400 / 3200 |
| Max voice length | 30 s | 5–120 s |
| Photo | 320×240, JPEG Q≈25, ≤ 12 kB | 160×120 … 640×480, cap 4–40 kB |
