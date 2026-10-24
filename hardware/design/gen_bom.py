#!/usr/bin/env python3
"""Costed BOM for the Nomiq A0 board + device, and JLCPCB assembly files.

    python3 hardware/design/gen_bom.py          # writes hardware/bom/bom-costed.csv, bom-summary.md,
                                                #        hardware/kicad/nomiq/fab/bom_jlc.csv, cpl_jlc.csv
Prices: USD, distributor list price on 2026-10-24 where a source is given (LCSC = lcsc.com product page),
otherwise an engineering estimate (source "est.") - re-quote before ordering. Board-level placement for the
CPL comes from the generated PCB file.
"""
from __future__ import annotations

import collections
import csv
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import nomiq_hw as HW  # noqa: E402

ROOT = HW.ROOT
# key: MPN (or value for generic passives) -> (LCSC#, price@1, price@100, source, availability note)
PRICES = {
    "Espressif ESP32-S3-WROOM-1-N16R8": ("C2913202", 4.60, 3.90, "est. (JLC extended part C2913202)", ""),
    "Semtech SX1262IMLTRT": ("C191341", 3.22, 1.89, "LCSC", "1.5k-31k in stock"),
    "TI BQ24074RGTR": ("C54313", 2.53, 1.78, "LCSC", "LCSC out of stock on 2026-10-24 - buy from TI/DigiKey or pre-order"),
    "TI TPS63001DRCR": ("C28060", 1.62, 1.01, "LCSC", "10k in stock"),
    "Analog Devices MAX17048G+T10": ("C2682616", 2.11, 1.51, "LCSC", "25k in stock"),
    "TI TCA9535PWR": ("C130204", 1.34, 0.92, "LCSC", "7k in stock"),
    "Analog Devices MAX98357AETE+T": ("C910544", 1.33, 0.95, "LCSC (qty-100 est.)", "22k in stock"),
    "TDK InvenSense ICS-43434": ("C5656610", 4.62, 2.20, "LCSC qty 1; qty-100 est.", "1.6k in stock"),
    "pSemi PE4259-63": ("C470892", 0.95, 0.60, "est. (LCSC C470892)", ""),
    "Epson TG2016SMN 32.0000M-ECGNNM3": ("", 2.79, 2.79, "Future Electronics (MOQ 250)",
                                         "0 stock, 20 wk lead - order early or use a JLC-stocked 32 MHz 1.8 V TG2016 variant"),
    "Molex 104031-0811": ("", 2.30, 1.75, "element14 (AUD 3.55 -> USD)", "15k in stock"),
    "GCT USB4105-GF-A": ("", 0.80, 0.55, "est.", ""),
    "STMicro USBLC6-2SC6": ("C7519", 0.12, 0.07, "est.", ""),
    "Littelfuse SMF5.0A": ("", 0.10, 0.05, "est.", ""),
    "JST S3B-PH-SM4-TB": ("", 0.20, 0.12, "est.", ""),
    "Taiyo Yuden NRS4018T2R2MDGJ": ("", 0.15, 0.08, "est.", ""),
    "TI TPS22918DBVR": ("", 0.35, 0.20, "est.", ""),
    "TI TLV75528PDBVR": ("", 0.15, 0.08, "est.", ""),
    "TI TLV75512PDBVR": ("", 0.15, 0.08, "est.", ""),
    "C&K PTS810 SJM 250 SMTR LFS": ("", 0.18, 0.11, "est.", ""),
    "Panasonic EVQ-P7A01P": ("", 0.35, 0.22, "est.", ""),
    "Tag-Connect TC2030-IDC-NL": ("", 0.0, 0.0, "footprint only (cable reused)", "no part placed"),
    "Murata LQM21FN150M70L": ("", 0.06, 0.03, "est.", ""),
    "Murata LQW15AN47NJ00": ("", 0.05, 0.02, "est.", ""),
    "Murata LQW15AN12NJ00": ("", 0.05, 0.02, "est.", ""),
    "Murata LQW15AN27NJ00": ("", 0.05, 0.02, "est.", ""),
    "Hirose U.FL-R-SMT-1(10)": ("", 0.25, 0.15, "est.", ""),
    "Infineon ESD0P2RF-02LS": ("", 0.12, 0.06, "est.", ""),
    "JST SM02B-SRSS-TB": ("", 0.12, 0.07, "est.", ""),
    "Hirose FH12-24S-0.5SH(55)": ("", 0.75, 0.45, "est.", ""),
    "Nexperia 2N7002,215": ("C8545", 0.02, 0.01, "est.", ""),
    "AOS AO3401A": ("C15127", 0.03, 0.02, "est.", ""),
    "Nexperia PESD5V0S1BL": ("", 0.05, 0.03, "est.", ""),
    "Samtec FTSH-105-01-L-DV": ("", 1.50, 1.00, "est.", "only if the STT SoM is fitted"),
    "2.4in 240x320 ILI9341 SPI module, 1x14 2.54 mm": ("", 5.00, 3.50, "est. (India retail ~INR 400-450)", "touch variant OK, touch unused"),
}
PASSIVE = {"R_0402": (0.002, 0.001), "C_0402": (0.003, 0.0015), "C_0603": (0.01, 0.005), "C_0805": (0.03, 0.015)}
OFFBOARD = [  # item, qty, price@1, price@100, source
    ("1S Li-ion pouch 3000 mAh (50x80x8 mm) with PCM + 10k NTC, JST-PH 3P", 1, 6.00, 4.00, "est."),
    ("Speaker 28 mm 8 ohm 1 W with JST-SH 2P lead", 1, 0.90, 0.50, "est."),
    ("OV2640 camera module, 24-pin 0.5 mm flex, 66-120 deg lens", 1, 4.00, 2.80, "est. (India retail)"),
    ("868 MHz antenna (SMA, 2-3 dBi) + u.FL-to-SMA bulkhead pigtail 100 mm", 1, 3.00, 1.80, "est."),
    ("microSD card 16 GB", 1, 3.50, 2.50, "est."),
    ("Enclosure FDM print (PETG/ASA, ~70 g) + 4 button caps", 1, 3.00, 2.00, "est. (material only)"),
    ("M2.5x16 screws x4 + M2.5 heat-set inserts x4", 1, 0.40, 0.20, "est."),
    ("STT co-processor SoM (ADR-0002, Phase 3)", 0, 15.00, 12.00, "est. - not in A0 build"),
]
PCB = [("PCB 4-layer 1.6 mm ENIG 64.6x124.6 mm (per board, qty 5 / qty 100)", 1, 8.00, 2.20, "est. (JLCPCB)"),
       ("SMT assembly both sides (setup amortised over 5 / 100) + extended-part fees", 1, 14.00, 2.50, "est. (JLCPCB)")]


def passive_key(fp):
    for k in PASSIVE:
        if fp.startswith(k):
            return k
    return None


def lines():
    g = collections.OrderedDict()
    for p in HW.PARTS.values():
        if p.dnp:
            continue
        fp = p.footprint.split(":")[-1]
        key = p.mpn or f"{p.value} {fp.split('_')[0]}_{fp.split('_')[1] if '_' in fp else ''}"
        g.setdefault((key, p.value, fp), []).append(p.ref)
    out = []
    for (key, value, fp), refs in g.items():
        if fp.startswith("TestPoint"):
            continue
        pk = passive_key(fp)
        if key in PRICES:
            lcsc, p1, p100, src, note = PRICES[key]
        elif pk:
            lcsc, (p1, p100), src, note = "", PASSIVE[pk], "est. (JLC basic part)", ""
        else:
            lcsc, p1, p100, src, note = "", 0.10, 0.05, "est.", "unpriced item"
        out.append(dict(refs=" ".join(refs), qty=len(refs), mpn=key if not pk or key in PRICES else "", value=value,
                        footprint=fp, lcsc=lcsc, p1=p1, p100=p100, src=src, note=note))
    return out


def write():
    L = lines()
    os.makedirs(os.path.join(ROOT, "hardware", "bom"), exist_ok=True)
    path = os.path.join(ROOT, "hardware", "bom", "bom-costed.csv")
    with open(path, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["designators", "qty", "mpn", "value", "footprint", "lcsc", "unit_usd_q1", "unit_usd_q100",
                    "ext_usd_q1", "ext_usd_q100", "price_source", "availability"])
        for r in L:
            w.writerow([r["refs"], r["qty"], r["mpn"], r["value"], r["footprint"], r["lcsc"], f"{r['p1']:.3f}",
                        f"{r['p100']:.3f}", f"{r['p1'] * r['qty']:.2f}", f"{r['p100'] * r['qty']:.2f}", r["src"], r["note"]])
        for name, qty, p1, p100, src in OFFBOARD + PCB:
            w.writerow(["(off-board)" if (name, qty, p1, p100, src) in OFFBOARD else "(fab)", qty, name, "", "", "",
                        f"{p1:.2f}", f"{p100:.2f}", f"{p1 * qty:.2f}", f"{p100 * qty:.2f}", src, ""])
    board1 = sum(r["p1"] * r["qty"] for r in L)
    board100 = sum(r["p100"] * r["qty"] for r in L)
    off1 = sum(q * a for _, q, a, _, _ in OFFBOARD)
    off100 = sum(q * b for _, q, _, b, _ in OFFBOARD)
    fab1 = sum(q * a for _, q, a, _, _ in PCB)
    fab100 = sum(q * b for _, q, _, b, _ in PCB)
    top = sorted(L, key=lambda r: -r["p1"] * r["qty"])[:8]
    md = [
        "# Nomiq A0 — cost summary (generated by hardware/design/gen_bom.py)", "",
        "USD, list prices on 2026-10-24 where sourced, otherwise estimates — re-quote before ordering.", "",
        "| Block | qty 1 (prototype) | qty 100 |", "|---|---:|---:|",
        f"| Board components ({sum(r['qty'] for r in L)} placed parts, {len(L)} lines) | {board1:.2f} | {board100:.2f} |",
        f"| PCB + assembly | {fab1:.2f} | {fab100:.2f} |",
        f"| Off-board (battery, speaker, camera, antenna, SD, enclosure, screws) | {off1:.2f} | {off100:.2f} |",
        f"| **Device total (without STT SoM)** | **{board1 + fab1 + off1:.2f}** | **{board100 + fab100 + off100:.2f}** |",
        "| STT co-processor SoM (Phase 3, ADR-0002) | +15.00 | +12.00 |", "",
        "## Largest line items (qty 1)", "",
        "| Item | refs | ext. USD |", "|---|---|---:|"]
    md += [f"| {r['mpn'] or r['value']} | {r['refs']} | {r['p1'] * r['qty']:.2f} |" for r in top]
    md += ["", "## Supply risks", ""]
    md += [f"- **{r['mpn']}** ({r['refs']}): {r['note']}" for r in L if r["note"] and ("stock" in r["note"] or "lead" in r["note"])
           and not r["note"].endswith("in stock")]
    md.append("")
    open(os.path.join(ROOT, "hardware", "bom", "bom-summary.md"), "w").write("\n".join(md))
    # JLC BOM
    fab = os.path.join(ROOT, "hardware", "kicad", "nomiq", "fab")
    os.makedirs(fab, exist_ok=True)
    with open(os.path.join(fab, "bom_jlc.csv"), "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        for r in L:
            if r["src"].startswith("footprint only"):
                continue
            w.writerow([r["value"] if not r["mpn"] else f"{r['value']} ({r['mpn']})", ",".join(r["refs"].split()), r["footprint"], r["lcsc"]])
    return board1, board100, off1 + fab1, off100 + fab100


def write_cpl():
    """pick-and-place from the generated PCB (KiCad page coords -> mm from the board's bottom-left aux origin)"""
    sys.path.insert(0, HERE)
    from gen_kicad import parse, find
    pcb = os.path.join(ROOT, "hardware", "kicad", "nomiq", "nomiq.kicad_pcb")
    if not os.path.exists(pcb):
        return 0
    root = parse(open(pcb).read())
    aux = [n for n in find(find(root, "setup")[0], "aux_axis_origin")][0]
    ox, oy = float(aux[1]), float(aux[2])
    n = 0
    with open(os.path.join(ROOT, "hardware", "kicad", "nomiq", "fab", "cpl_jlc.csv"), "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for fp in find(root, "footprint"):
            attr = find(fp, "attr")
            if attr and ("exclude_from_pos_files" in attr[0] or "dnp" in attr[0]):
                continue
            ref = [p[2] for p in find(fp, "property") if p[1] == "Reference"][0]
            at = find(fp, "at")[0]
            x, y = float(at[1]) - ox, oy - float(at[2])
            side = "Top" if find(fp, "layer")[0][1] == "F.Cu" else "Bottom"
            w.writerow([ref, f"{x:.3f}mm", f"{y:.3f}mm", side, f"{float(at[3]) if len(at) > 3 else 0:.0f}"])
            n += 1
    return n


if __name__ == "__main__":
    b1, b100, o1, o100 = write()
    n = write_cpl()
    print(f"board parts: ${b1:.2f} (qty 1) / ${b100:.2f} (qty 100); device total ${b1 + o1:.2f} / ${b100 + o100:.2f}; "
          f"CPL {n} parts")
