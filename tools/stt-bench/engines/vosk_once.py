#!/usr/bin/env python3
"""One-shot Vosk transcription: prints the final transcript on the last line."""
import argparse
import json
import wave

from vosk import KaldiRecognizer, Model, SetLogLevel  # pip install vosk

ap = argparse.ArgumentParser()
ap.add_argument("--model", required=True)
ap.add_argument("wav")
a = ap.parse_args()
SetLogLevel(-1)
wf = wave.open(a.wav, "rb")
rec = KaldiRecognizer(Model(a.model), wf.getframerate())
while True:
    data = wf.readframes(4000)
    if not data:
        break
    rec.AcceptWaveform(data)
print(json.loads(rec.FinalResult()).get("text", ""))
