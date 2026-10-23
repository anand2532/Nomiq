# ADR-0003: 865–867 MHz, 125 kHz BW, dual regulatory profiles

* Status: accepted (legal confirmation pending)
* Date: 2026-10-23
* Requirements: REQ-RF-002

## Context
India: G.S.R. 564(E) 2008 (865–867 MHz, ≤ 1 W / 4 W ERP, ≤ 200 kHz) and G.S.R. 853(E) 2021 SRD rules (865–868 MHz, duty-cycle ≤ 2.5 % for non-AP devices in the data-acquisition class, APC). Which applies to a P2P messenger must be confirmed with WPC / test lab.

## Decision
* 125 kHz bandwidth only (250/500 kHz excluded by 200 kHz limit).
* Firmware supports `IN865_2008` (no duty limiter) and `IN865_SRD2021` (2.5 % rolling-hour limiter + APC). Default for development: `IN865_2008`; ship default decided after legal review.
* +22 dBm max conducted; APC lowers power when SNR margin > 10 dB.

## Consequences
Under the 2021 profile, photos/voice take ~40× longer — UI shows ETA; smaller default photo (160×120) in that profile.

## Validation
Regulatory consultation before ETA application; conducted power + occupied bandwidth measured in Phase 7d.
