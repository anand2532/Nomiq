#!/usr/bin/env python3
"""LoRa time-on-air and Nomiq transfer-time estimator.

Implements the Semtech SX126x time-on-air formula (AN1200.13 / SX1262 datasheet
section 6.1.4) and estimates end-to-end transfer time for Nomiq messages using
the fragment/ACK scheme in docs/specs/communication-protocol.md.

Examples:
    python3 tools/lora_airtime.py --table
    python3 tools/lora_airtime.py --sf 9 --bw 125 --bytes 12000
    python3 tools/lora_airtime.py --sf 7 --bw 250 --bytes 4500 --duty 2.5
"""
from __future__ import annotations

import argparse
import math

# Protocol constants — keep in sync with firmware/components/nomiq_proto/include/nomiq_proto.h
LORA_MAX_PAYLOAD = 255
HEADER_BYTES = 20          # nomiq_hdr_t
SEC_BYTES = 12             # frame counter (4) + AES-CCM tag (8)
FRAG_PAYLOAD = LORA_MAX_PAYLOAD - HEADER_BYTES - SEC_BYTES  # 223
ACK_FRAME_BYTES = HEADER_BYTES + SEC_BYTES + 10             # SACK: 64-bit bitmap + snr + rssi
FRAGS_PER_WINDOW = 16      # sender sends a burst, then waits for a SACK
TURNAROUND_S = 0.030       # TX->RX switch + processing guard per exchange


def time_on_air(payload: int, sf: int, bw_khz: float, cr: int = 1, preamble: int = 8,
                explicit_header: bool = True, crc: bool = True) -> float:
    """Return time on air in seconds. cr: 1..4 for 4/5..4/8."""
    bw = bw_khz * 1000.0
    t_sym = (2 ** sf) / bw
    ldro = 1 if (t_sym > 0.016) else 0  # low data-rate optimise (SF11/12 @125k)
    ih = 0 if explicit_header else 1
    crc_bits = 16 if crc else 0
    if sf in (5, 6):
        num = 8 * payload + crc_bits - 4 * sf + 20 * ih
        n_payload = 8 + max(math.ceil(num / (4 * sf)) * (cr + 4), 0)
        t_preamble = (preamble + 6.25) * t_sym
    else:
        num = 8 * payload + crc_bits - 4 * sf + 8 + 20 * ih
        n_payload = 8 + max(math.ceil(num / (4 * (sf - 2 * ldro))) * (cr + 4), 0)
        t_preamble = (preamble + 4.25) * t_sym
    return t_preamble + n_payload * t_sym


def transfer_time(nbytes: int, sf: int, bw_khz: float, cr: int = 1, per: float = 0.0,
                  duty_pct: float | None = None) -> dict:
    """Estimate transfer of an object of nbytes with windowed selective ACK.

    per: packet error rate (0..1); retransmissions modelled as 1/(1-per) inflation.
    duty_pct: if set, airtime is spread so TX airtime <= duty_pct % of wall time.
    """
    n_frags = max(1, math.ceil(nbytes / FRAG_PAYLOAD))
    last = nbytes - (n_frags - 1) * FRAG_PAYLOAD
    t_full = time_on_air(LORA_MAX_PAYLOAD, sf, bw_khz, cr)
    t_last = time_on_air(HEADER_BYTES + SEC_BYTES + last, sf, bw_khz, cr)
    t_ack = time_on_air(ACK_FRAME_BYTES, sf, bw_khz, cr)
    windows = math.ceil(n_frags / FRAGS_PER_WINDOW)
    tx_air = ((n_frags - 1) * t_full + t_last) / (1 - per)
    rx_air = windows * t_ack / (1 - per)
    wall = tx_air + rx_air + windows * 2 * TURNAROUND_S + n_frags * 0.005
    if duty_pct:
        wall = max(wall, tx_air / (duty_pct / 100.0))
    return {
        "fragments": n_frags,
        "toa_full_ms": t_full * 1000,
        "tx_airtime_s": tx_air,
        "wall_s": wall,
        "goodput_bps": nbytes * 8 / wall,
    }


def table() -> None:
    # India limits carrier bandwidth to 200 kHz, so BW250/500 are NOT usable — 125 kHz only.
    presets = [(7, 125), (8, 125), (9, 125), (10, 125), (11, 125), (12, 125)]
    objs = [("text 140 B", 140), ("voice 15 s Codec2-1200 (2.3 kB)", 2250),
            ("voice 30 s Codec2-1200 (4.5 kB)", 4500), ("photo 160x120 Q30 (4 kB)", 4000),
            ("photo 320x240 Q25 (12 kB)", 12000), ("photo 320x240 Q50 (20 kB)", 20000)]
    print(f"{'SF/BW':>9} {'ToA255':>8} | " + " | ".join(f"{n[:22]:>22}" for n, _ in objs))
    for sf, bw in presets:
        row = []
        for _, b in objs:
            r = transfer_time(b, sf, bw, per=0.1)
            row.append(f"{r['wall_s']:>20.1f} s")
        print(f"SF{sf:<2}/{bw:<4} {time_on_air(255, sf, bw) * 1000:>6.0f}ms | " + " | ".join(row))
    print("\n(assumes CR4/5, 8-sym preamble, 10% PER, no duty-cycle limit, 223 B payload/fragment)")
    print("With a 2.5% duty-cycle profile multiply TX airtime by 40 — see docs/specs/lora-link-budget.md")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--sf", type=int, default=9)
    p.add_argument("--bw", type=float, default=125)
    p.add_argument("--cr", type=int, default=1, help="1..4 => 4/5..4/8")
    p.add_argument("--bytes", type=int, default=255)
    p.add_argument("--per", type=float, default=0.0)
    p.add_argument("--duty", type=float, default=None, help="duty-cycle limit in percent")
    p.add_argument("--table", action="store_true")
    a = p.parse_args()
    if a.table:
        table()
        return
    r = transfer_time(a.bytes, a.sf, a.bw, a.cr, a.per, a.duty)
    print(f"SF{a.sf} BW{a.bw:g}kHz CR4/{a.cr + 4}: {a.bytes} B -> {r['fragments']} fragments")
    print(f"  ToA(255 B frame) : {r['toa_full_ms']:.1f} ms")
    print(f"  TX airtime       : {r['tx_airtime_s']:.1f} s")
    print(f"  wall-clock       : {r['wall_s']:.1f} s  (goodput {r['goodput_bps']:.0f} bit/s)")


if __name__ == "__main__":
    main()
