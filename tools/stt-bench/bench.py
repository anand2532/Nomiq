#!/usr/bin/env python3
"""Offline STT benchmark harness — Phase 3 gate (docs/specs/speech-to-text.md §3).

Runs each configured engine over a test set recorded through the real Nomiq mic and
reports corpus WER/CER, real-time factor, latency, peak RSS and (optionally) energy.
Stdlib only, so it runs unchanged on a Pi Zero 2 W / RV1106 board.

    python3 bench.py --manifest testset/manifest.csv --engines engines.json --out results/
    python3 bench.py --self-test        # pipeline check with a fake engine

manifest.csv:  wav,text,condition,speaker   (wav paths relative to the manifest)
engines.json:  see engines.example.json — each engine is a shell command template with
               {wav} (and optional {lang}); stdout is the transcript (last non-empty line
               unless "output_regex" is given).

Energy: if --power-cmd is given it is started before each engine run and must print the
average power in mW on exit (e.g. a PPK2 / INA219 logger script); mWh per utterance is
reported. Without it, energy columns stay empty — fill them from the bench supply log.
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import re
import resource
import shlex
import signal
import subprocess
import sys
import tempfile
import time
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wer import cer, corpus_wer, wer  # noqa: E402

PASS_WER_QUIET = 0.15
PASS_WER_NOISE = 0.30
PASS_LATENCY_X = 2.0


def wav_seconds(path: str) -> float:
    with wave.open(path, "rb") as w:
        return w.getnframes() / float(w.getframerate())


def run_engine(cmd_t: str, wav: str, lang: str, timeout: float, regex: str | None, power_cmd: str | None):
    cmd = cmd_t.format(wav=shlex.quote(wav), lang=lang)
    pm = None
    if power_cmd:
        pm = subprocess.Popen(shlex.split(power_cmd), stdout=subprocess.PIPE, text=True)
    before = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    t0 = time.monotonic()
    try:
        p = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout)
        out, rc = p.stdout, p.returncode
    except subprocess.TimeoutExpired:
        out, rc = "", -1
    dt = time.monotonic() - t0
    rss_kb = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    mw = None
    if pm:
        pm.send_signal(signal.SIGINT)
        try:
            mw = float((pm.communicate(timeout=5)[0] or "nan").strip().splitlines()[-1])
        except Exception:  # noqa: BLE001
            mw = None
    if regex:
        m = re.search(regex, out, re.S)
        text = m.group(1).strip() if m else ""
    else:
        lines = [l.strip() for l in out.splitlines() if l.strip()]
        text = lines[-1] if lines else ""
    return text, dt, max(rss_kb, before), rc, mw


def load_manifest(path: str) -> list[dict]:
    base = os.path.dirname(os.path.abspath(path))
    rows = []
    with open(path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            r["wav"] = os.path.join(base, r["wav"])
            r.setdefault("condition", "quiet")
            rows.append(r)
    return rows


def bench(manifest: list[dict], engines: list[dict], out_dir: str, power_cmd: str | None, lang: str) -> list[dict]:
    os.makedirs(out_dir, exist_ok=True)
    summary = []
    for eng in engines:
        name = eng["name"]
        if eng.get("warmup", True) and manifest:  # load model / page cache once
            run_engine(eng["cmd"], manifest[0]["wav"], lang, eng.get("timeout", 120), eng.get("output_regex"), None)
        per = []
        for r in manifest:
            secs = wav_seconds(r["wav"])
            hyp, dt, rss, rc, mw = run_engine(eng["cmd"], r["wav"], lang, eng.get("timeout", 120),
                                              eng.get("output_regex"), power_cmd)
            per.append({
                "engine": name, "wav": os.path.basename(r["wav"]), "condition": r["condition"],
                "speaker": r.get("speaker", ""), "audio_s": round(secs, 2), "latency_s": round(dt, 3),
                "rtf": round(dt / secs, 3) if secs else 0, "wer": round(wer(r["text"], hyp), 4),
                "cer": round(cer(r["text"], hyp), 4), "peak_rss_mb": round(rss / 1024, 1), "rc": rc,
                "energy_mwh": round(mw * dt / 3600, 3) if mw else "", "ref": r["text"], "hyp": hyp,
            })
        with open(os.path.join(out_dir, f"{name}.csv"), "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=list(per[0].keys()))
            w.writeheader()
            w.writerows(per)
        rows = {"engine": name}
        for cond in ("quiet", "noise"):
            sel = [p for p in per if p["condition"] == cond]
            m = [r for r in manifest if r["condition"] == cond]
            rows[f"wer_{cond}"] = round(corpus_wer([(r["text"], p["hyp"]) for r, p in zip(m, sel)]), 4) if sel else None
        rows["rtf_mean"] = round(sum(p["rtf"] for p in per) / len(per), 3)
        rows["latency_max_s"] = max(p["latency_s"] for p in per)
        rows["peak_rss_mb"] = max(p["peak_rss_mb"] for p in per)
        e = [p["energy_mwh"] for p in per if p["energy_mwh"] != ""]
        rows["energy_mwh_mean"] = round(sum(e) / len(e), 3) if e else None
        rows["failures"] = sum(1 for p in per if p["rc"] != 0)
        rows["pass"] = bool(
            (rows["wer_quiet"] is None or rows["wer_quiet"] <= PASS_WER_QUIET)
            and (rows["wer_noise"] is None or rows["wer_noise"] <= PASS_WER_NOISE)
            and all(p["rtf"] <= PASS_LATENCY_X for p in per)
            and rows["failures"] == 0
        )
        summary.append(rows)
    with open(os.path.join(out_dir, "summary.md"), "w", encoding="utf-8") as f:
        f.write("| Engine | WER quiet | WER noise | mean RTF | max latency s | peak RSS MB | mWh/utt | fails | PASS |\n")
        f.write("|---|---|---|---|---|---|---|---|---|\n")
        for s in summary:
            f.write(f"| {s['engine']} | {s['wer_quiet']} | {s['wer_noise']} | {s['rtf_mean']} | {s['latency_max_s']} "
                    f"| {s['peak_rss_mb']} | {s['energy_mwh_mean'] or ''} | {s['failures']} | {'✅' if s['pass'] else '❌'} |\n")
    return summary


def self_test() -> int:
    """Pipeline check: synthetic wavs + a fake engine that echoes a canned transcript."""
    with tempfile.TemporaryDirectory() as d:
        refs = ["send the photo to ravi", "meet me at the north gate at five", "battery is low"]
        hyps = ["send the photo to ravi", "meet me at north gate at five", "battery is slow"]
        with open(os.path.join(d, "manifest.csv"), "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["wav", "text", "condition", "speaker"])
            for i, ref in enumerate(refs):
                wp = os.path.join(d, f"u{i}.wav")
                with wave.open(wp, "wb") as wv:
                    wv.setnchannels(1); wv.setsampwidth(2); wv.setframerate(16000)
                    wv.writeframes(b"\x00\x00" * 16000 * 2)  # 2 s
                with open(wp + ".txt", "w") as t:
                    t.write(hyps[i])
                w.writerow([f"u{i}.wav", ref, "quiet", "s1"])
        engines = [{"name": "fake", "cmd": "echo loading model; cat {wav}.txt", "warmup": False}]
        s = bench(load_manifest(os.path.join(d, "manifest.csv")), engines, os.path.join(d, "out"), None, "en")[0]
        expect = corpus_wer(list(zip(refs, hyps)))
        ok = abs(s["wer_quiet"] - round(expect, 4)) < 1e-9 and s["failures"] == 0 and s["pass"] is True
        print(open(os.path.join(d, "out", "summary.md")).read())
        print("self-test", "OK" if ok else "FAILED", f"(corpus WER {expect:.3f})")
        return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--manifest")
    ap.add_argument("--engines", default=os.path.join(os.path.dirname(__file__), "engines.example.json"))
    ap.add_argument("--out", default="results")
    ap.add_argument("--lang", default="en")
    ap.add_argument("--power-cmd")
    ap.add_argument("--only", help="comma-separated engine names")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if not a.manifest:
        ap.error("--manifest required")
    engines = json.load(open(a.engines))["engines"]
    if a.only:
        keep = set(a.only.split(","))
        engines = [e for e in engines if e["name"] in keep]
    for s in bench(load_manifest(a.manifest), engines, a.out, a.power_cmd, a.lang):
        print(s)
    print(f"summary: {os.path.join(a.out, 'summary.md')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
