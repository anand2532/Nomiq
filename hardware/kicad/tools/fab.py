#!/usr/bin/env python3
"""Zone fill + DRC + fabrication outputs for the Nomiq board. Runs where KiCad 8+ is installed
(CI: kicad/kicad:8.0 container; locally: KiCad's bundled python).

    python3 hardware/kicad/tools/fab.py [--out hardware/kicad/nomiq/fab/out]

1. load nomiq.kicad_pcb with pcbnew, fill all zones, save as fab/out/nomiq_filled.kicad_pcb
2. kicad-cli pcb drc (json + report) on the filled board -> fab/out/drc.json, drc.rpt
3. gerbers (all copper, mask, paste, silk, edge), drill (excellon + map), position file, STEP, PDF
4. zip gerbers+drill for the fab house -> fab/out/nomiq-a0-gerbers.zip
Exit code 1 if DRC reports errors (warnings are listed but do not fail).
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
PRJ = os.path.abspath(os.path.join(HERE, "..", "nomiq"))
PCB = os.path.join(PRJ, "nomiq.kicad_pcb")
LAYERS = "F.Cu,In1.Cu,In2.Cu,B.Cu,F.Mask,B.Mask,F.Paste,B.Paste,F.SilkS,B.SilkS,Edge.Cuts"


def run(cmd):
    print("+", " ".join(cmd), flush=True)
    return subprocess.run(cmd, check=False)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(PRJ, "fab", "out"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    import pcbnew  # noqa: PLC0415  (only available with KiCad)
    board = pcbnew.LoadBoard(PCB)
    filler = pcbnew.ZONE_FILLER(board)
    filler.Fill(board.Zones())
    filled = os.path.join(a.out, "nomiq_filled.kicad_pcb")
    pcbnew.SaveBoard(filled, board)
    for ext in (".kicad_pro", ".kicad_prl"):
        src = os.path.join(PRJ, "nomiq" + ext)
        if os.path.exists(src):
            shutil.copy(src, os.path.join(a.out, "nomiq_filled" + ext))
    print(f"zones filled: {len(board.Zones())}", flush=True)

    run(["kicad-cli", "pcb", "drc", "--format", "json", "--severity-all", "--units", "mm", "-o",
         os.path.join(a.out, "drc.json"), filled])
    run(["kicad-cli", "pcb", "drc", "--format", "report", "--severity-all", "--units", "mm", "-o",
         os.path.join(a.out, "drc.rpt"), filled])
    g = os.path.join(a.out, "gerbers")
    os.makedirs(g, exist_ok=True)
    run(["kicad-cli", "pcb", "export", "gerbers", "--layers", LAYERS, "--subtract-soldermask", "--use-drill-file-origin",
         "-o", g + "/", filled])
    run(["kicad-cli", "pcb", "export", "drill", "--format", "excellon", "--drill-origin", "plot", "--excellon-separate-th",
         "--generate-map", "--map-format", "pdf", "-o", g + "/", filled])
    run(["kicad-cli", "pcb", "export", "pos", "--format", "csv", "--units", "mm", "--side", "both", "--use-drill-file-origin",
         "-o", os.path.join(a.out, "nomiq-pos.csv"), filled])
    run(["kicad-cli", "pcb", "export", "step", "--subst-models", "--force", "-o", os.path.join(a.out, "nomiq-a0.step"), filled])
    run(["kicad-cli", "pcb", "export", "pdf", "--layers", "F.Cu,B.Cu,F.Fab,B.Fab,Edge.Cuts", "-o",
         os.path.join(a.out, "nomiq-a0-layers.pdf"), filled])
    z = os.path.join(a.out, "nomiq-a0-gerbers.zip")
    with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as zf:
        for fn in sorted(os.listdir(g)):
            zf.write(os.path.join(g, fn), fn)
    print(f"gerber zip: {z}")

    errors = warnings = 0
    unconnected = 0
    try:
        rep = json.load(open(os.path.join(a.out, "drc.json")))
        for v in rep.get("violations", []):
            if v.get("severity") == "error":
                errors += 1
            else:
                warnings += 1
        unconnected = len(rep.get("unconnected_items", []))
        by_type = {}
        for v in rep.get("violations", []):
            by_type[(v.get("severity"), v.get("type"))] = by_type.get((v.get("severity"), v.get("type")), 0) + 1
        for (sev, typ), n in sorted(by_type.items()):
            print(f"DRC {sev:8s} {typ:30s} {n}")
    except Exception as e:  # noqa: BLE001
        print("could not read drc.json:", e)
        errors = 1
    print(f"DRC: {errors} errors, {warnings} warnings, {unconnected} unconnected")
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a") as fh:
            fh.write(f"### KiCad DRC (zones filled)\n\n{errors} errors, {warnings} warnings, {unconnected} unconnected items\n")
    return 1 if errors or unconnected else 0


if __name__ == "__main__":
    sys.exit(main())
