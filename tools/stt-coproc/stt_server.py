#!/usr/bin/env python3
"""Nomiq STT co-processor service (runs on the power-gated Linux SoM).

Boots with the SoM, opens the UART to the ESP32-S3, answers HELLO with READY and turns
AUDIO_BEGIN / AUDIO_CHUNK* / AUDIO_END into a FINAL transcript using any engine command
from tools/stt-bench/engines.json (same templates as the benchmark). No network needed.

    python3 stt_server.py --port /dev/ttyS0 --baud 921600 --engines engines.json --engine vosk-small-en
    python3 stt_server.py --stdio ...        # for tests: frames on stdin/stdout

Install as a systemd service (see README.md) so READY is sent as early as possible after boot.
"""
from __future__ import annotations

import argparse
import json
import os
import shlex
import subprocess
import sys
import tempfile
import time
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sttproto as P  # noqa: E402


class Session:
    def __init__(self, engine: dict, write, poweroff: bool = False) -> None:
        self.engine = engine
        self.write = write
        self.poweroff = poweroff
        self.audio = bytearray()
        self.rate = 16000
        self.lang = "en"
        self.active = False

    def send(self, ftype: int, payload: bytes = b"") -> None:
        self.write(P.encode(ftype, payload))

    def error(self, code: int, msg: str) -> None:
        self.send(P.ERROR, bytes([code]) + msg.encode()[:200])

    def transcribe(self) -> tuple[str, float]:
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
            path = f.name
        try:
            with wave.open(path, "wb") as w:
                w.setnchannels(1); w.setsampwidth(2); w.setframerate(self.rate)
                w.writeframes(bytes(self.audio))
            cmd = self.engine["cmd"].format(wav=shlex.quote(path), lang=self.lang)
            p = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=self.engine.get("timeout", 120))
            lines = [l.strip() for l in p.stdout.splitlines() if l.strip()]
            text = lines[-1] if lines else ""
            if p.returncode != 0:
                raise RuntimeError(p.stderr.strip()[-200:] or f"engine rc {p.returncode}")
            # engines here do not expose calibrated confidence; empty text => 0
            return text, (0.9 if text else 0.0)
        finally:
            os.unlink(path)

    def handle(self, ftype: int, payload: bytes) -> bool:
        """Returns False when the service should exit."""
        if ftype == P.HELLO:
            self.send(P.READY, self.engine["name"].encode())
        elif ftype == P.AUDIO_BEGIN:
            self.rate, self.lang = P.parse_audio_begin(payload)
            self.audio.clear()
            self.active = True
        elif ftype == P.AUDIO_CHUNK and self.active:
            self.audio += payload
            if len(self.audio) > self.rate * 2 * 130:      # > 130 s: refuse
                self.active = False
                self.error(2, "audio too long")
        elif ftype == P.AUDIO_END and self.active:
            self.active = False
            t0 = time.monotonic()
            try:
                text, conf = self.transcribe()
                self.send(P.FINAL, P.final_payload(text, conf))
            except Exception as e:  # noqa: BLE001
                self.error(1, str(e))
            sys.stderr.write(f"transcribed {len(self.audio) / (2 * self.rate):.1f}s in {time.monotonic() - t0:.2f}s\n")
        elif ftype == P.SHUTDOWN:
            if self.poweroff:
                subprocess.run(["sudo", "-n", "poweroff"], check=False)
            return False
        return True


def open_uart(dev: str, baud: int):
    import termios
    import tty
    fd = os.open(dev, os.O_RDWR | os.O_NOCTTY)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    speed = getattr(termios, f"B{baud}")
    attrs[4] = attrs[5] = speed
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="/dev/ttyS0")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--stdio", action="store_true")
    ap.add_argument("--engines", default=os.path.join(os.path.dirname(__file__), "..", "stt-bench", "engines.example.json"))
    ap.add_argument("--engine", required=True)
    ap.add_argument("--poweroff-on-shutdown", action="store_true")
    a = ap.parse_args()
    engines = {e["name"]: e for e in json.load(open(a.engines))["engines"]}
    if a.engine not in engines:
        ap.error(f"unknown engine {a.engine}; have {', '.join(engines)}")

    if a.stdio:
        rfd, wfd = sys.stdin.fileno(), sys.stdout.fileno()
    else:
        rfd = wfd = open_uart(a.port, a.baud)
    sess = Session(engines[a.engine], lambda b: os.write(wfd, b), a.poweroff_on_shutdown)
    sess.send(P.READY, a.engine.encode())          # unsolicited READY at boot
    parser = P.Parser()
    while True:
        data = os.read(rfd, 4096)
        if not data:
            return 0
        for ftype, payload in parser.feed(data):
            if not sess.handle(ftype, payload):
                return 0


if __name__ == "__main__":
    sys.exit(main())
