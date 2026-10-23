# Master Test Plan

| Test ID | Title | REQ | Method | Pass criterion | Phase |
|---|---|---|---|---|---|
| TP-RF-01 | Text P2P delivery & ACK | REQ-RF-003/004, SYS-002 | T | ≥ 99 % delivered of 1000 msgs @ 500 m LOS | 1 |
| TP-RF-02 | Fragmentation under loss | REQ-RF-005/006 | T (host + air) | Object intact at 30 % PER (host test `test_lossy_transfer`) | 1 |
| TP-RF-03 | Resume after reboot | REQ-RF-008, STO-003 | T | Transfer completes after RX power-cycle at 50 % | 4 |
| TP-RF-04 | Priority | REQ-RF-010 | T | Text sent during photo TX delivered < 5 s | 4 |
| TP-RF-05 | Range / PER vs SF | REQ-RF-002 | T | Log RSSI/SNR/PER at 0.1–5 km, SF7–SF12 | 1, 7d |
| TP-RF-06 | Conducted power, OBW, frequency error | REQ-RF-002 | T | ≤ +22 dBm, OBW ≤ 200 kHz, ±2 ppm | 7d |
| TP-SEC-01 | Encryption & auth | REQ-RF-009 | T | Unpaired / tampered frames rejected | 4 |
| TP-STT-01 | WER & latency | REQ-STT-001/006 | T | WER ≤ 15 % quiet; latency ≤ 2× audio | 3 |
| TP-AUD-01 | Voice note round trip | REQ-AUD-* | T/D | 30 s note intelligible, delivered at SF9 | 4 |
| TP-CAM-01 | Photo size & transfer | REQ-CAM-* | T | 320×240 ≤ 12 kB; delivered & verified | 5 |
| TP-STO-01 | Storage full | REQ-STO-004 | T | Bulk rejected, text accepted, no crash | 6 |
| TP-PWR-01 | Idle current | REQ-PWR-005 | T | ≤ 3 mA | 6, 7d |
| TP-PWR-02 | Battery life (scripted day) | REQ-PWR-* | T | ≥ 3 days | 7d |
| TP-PWR-03 | Charging & protection | REQ-PWR-002/003 | T | CC/CV correct, protection trips | 7d |
| TP-MEC-01 | Fit & openings | REQ-MEC-* | I | All features aligned ±0.3 mm | 7c |
| TP-SYS-01 | Standalone operation | REQ-SYS-001 | D | All modes with no phone/network | 7d |
