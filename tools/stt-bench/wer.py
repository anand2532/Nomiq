"""Word / character error rate (stdlib only, no jiwer needed on the SoM).

WER = (S + D + I) / N over normalised words (lowercase, punctuation stripped,
digits kept). Matches jiwer's default WER for the normalisation used here.
"""
from __future__ import annotations

import re
import unicodedata

_PUNCT = re.compile(r"[^\w\s']", re.UNICODE)


def normalise(text: str) -> list[str]:
    t = unicodedata.normalize("NFKC", text).lower()
    t = _PUNCT.sub(" ", t)
    t = t.replace("'", "")
    return t.split()


def _edits(ref: list, hyp: list) -> tuple[int, int, int, int]:
    """Return (substitutions, deletions, insertions, ref_len) via Levenshtein alignment."""
    n, m = len(ref), len(hyp)
    # dp[i][j] = (cost, S, D, I)
    prev = [(j, 0, 0, j) for j in range(m + 1)]
    for i in range(1, n + 1):
        cur = [(i, 0, i, 0)] + [None] * m
        for j in range(1, m + 1):
            if ref[i - 1] == hyp[j - 1]:
                cur[j] = prev[j - 1]
                continue
            s = prev[j - 1]
            d = prev[j]
            ins = cur[j - 1]
            best = min(
                (s[0] + 1, s[1] + 1, s[2], s[3]),
                (d[0] + 1, d[1], d[2] + 1, d[3]),
                (ins[0] + 1, ins[1], ins[2], ins[3] + 1),
            )
            cur[j] = best
        prev = cur
    c = prev[m]
    return c[1], c[2], c[3], n


def wer_counts(ref: str, hyp: str) -> tuple[int, int, int, int]:
    return _edits(normalise(ref), normalise(hyp))


def wer(ref: str, hyp: str) -> float:
    s, d, i, n = wer_counts(ref, hyp)
    return (s + d + i) / n if n else float(bool(normalise(hyp)))


def cer(ref: str, hyp: str) -> float:
    r = list(" ".join(normalise(ref)))
    h = list(" ".join(normalise(hyp)))
    s, d, i, n = _edits(r, h)
    return (s + d + i) / n if n else float(bool(h))


def corpus_wer(pairs: list[tuple[str, str]]) -> float:
    """Corpus-level WER: total edits / total reference words (not mean of per-utterance WER)."""
    e = t = 0
    for ref, hyp in pairs:
        s, d, i, n = wer_counts(ref, hyp)
        e += s + d + i
        t += n
    return e / t if t else 0.0
