"""Nomiq ESP32 <-> STT co-processor UART framing (mirror of firmware/components/stt_link/stt_proto.c).

Frame: 0xA5 | type u8 | len u32 LE | payload | crc16 LE   (CRC-16/CCITT-FALSE over type+len+payload)
"""
from __future__ import annotations

import struct

SOF = 0xA5
MAX_PAYLOAD = 4096

HELLO, READY = 0x01, 0x02
AUDIO_BEGIN, AUDIO_CHUNK, AUDIO_END = 0x10, 0x11, 0x12
PARTIAL, FINAL = 0x20, 0x21
SHUTDOWN = 0x30
ERROR = 0x7F


def crc16(data: bytes, crc: int = 0xFFFF) -> int:
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode(ftype: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too large")
    body = struct.pack("<BI", ftype, len(payload)) + payload
    return bytes([SOF]) + body + struct.pack("<H", crc16(body))


def final_payload(text: str, confidence: float) -> bytes:
    return struct.pack("<H", max(0, min(1000, int(round(confidence * 1000))))) + text.encode("utf-8")


def parse_audio_begin(p: bytes) -> tuple[int, str]:
    rate, lang = struct.unpack("<I8s", p[:12])
    return rate, lang.rstrip(b"\0").decode()


class Parser:
    """Byte-stream parser with resynchronisation on SOF."""

    def __init__(self) -> None:
        self.buf = bytearray()
        self.crc_errors = 0

    def feed(self, data: bytes):
        self.buf += data
        out = []
        while True:
            i = self.buf.find(bytes([SOF]))
            if i < 0:
                self.buf.clear()
                break
            del self.buf[:i]
            if len(self.buf) < 6:
                break
            ftype, n = struct.unpack_from("<BI", self.buf, 1)
            if n > MAX_PAYLOAD:
                del self.buf[:1]
                continue
            if len(self.buf) < 6 + n + 2:
                break
            body = bytes(self.buf[1:6 + n])
            (crc,) = struct.unpack_from("<H", self.buf, 6 + n)
            if crc != crc16(body):
                self.crc_errors += 1
                del self.buf[:1]
                continue
            out.append((ftype, body[5:]))
            del self.buf[:6 + n + 2]
        return out
