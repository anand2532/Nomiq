# STT benchmark (Phase 3 gate)

Decides ADR-0002: which offline speech-to-text engine and co-processor Nomiq ships with.
Pass criteria (speech-to-text spec §3): corpus WER ≤ 15 % quiet / ≤ 30 % noise, latency ≤ 2× audio length.

## 1. Record the test set
- 5 speakers × 60 prompts from `prompts_en.txt` (+ regional language list if required), **through the actual MEMS mic** on the dev kit, 16 kHz mono 16-bit WAV.
- Repeat a subset (≥ 20 per speaker) with 60 dB(A) babble noise playback at 1 m.
- `manifest.csv`: `wav,text,condition,speaker` (condition = quiet | noise).

## 2. Install engines on each candidate board
| Engine | Install |
|---|---|
| Vosk | `pip install vosk`; model `vosk-model-small-en-in-0.4` (Indian English) or `small-en-us` |
| whisper.cpp | `git clone https://github.com/ggml-org/whisper.cpp && cmake -B build && cmake --build build -j`; `models/download-ggml-model.sh tiny.en` then quantise to q5_1 |
| sherpa-onnx | prebuilt aarch64/armv7 release; streaming/offline Zipformer English 20M int8 |

Copy `engines.example.json` → `engines.json` and fix model paths.

## 3. Run
```bash
python3 bench.py --self-test                                   # harness check
python3 bench.py --manifest testset/manifest.csv --engines engines.json --out results/<board>
python3 bench.py ... --power-cmd "python3 ppk2_avg.py"          # optional energy per utterance
```
Outputs `results/<board>/<engine>.csv` (per utterance) and `summary.md` — paste into `docs/specs/speech-to-text.md` and `tests/reports/phase3-stt.md`.

Cold-start latency (boot → READY) is measured separately with the co-processor service in `tools/stt-coproc/`.
