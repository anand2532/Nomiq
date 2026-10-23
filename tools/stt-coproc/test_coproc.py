import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sttproto as P  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


class ProtoTest(unittest.TestCase):
    def test_vectors_match_c(self):
        # same vectors as firmware/components/stt_link/test/test_stt_proto.c
        self.assertEqual(P.encode(P.FINAL, P.final_payload("hello nomiq", 0.873)).hex(),
                         "a5210d000000690368656c6c6f206e6f6d6971d0ed")
        self.assertEqual(P.encode(P.AUDIO_END).hex(), "a51200000000d551")

    def test_parser_resync(self):
        p = P.Parser()
        stream = b"\x00\xa5\xff\x13" + P.encode(P.FINAL, b"\x00\x00ok")
        bad = bytearray(P.encode(P.PARTIAL, b"abc")); bad[7] ^= 1
        stream += bytes(bad) + P.encode(P.READY, b"m")
        frames = []
        for i in range(0, len(stream), 3):            # arbitrary chunking
            frames += p.feed(stream[i:i + 3])
        self.assertEqual([f[0] for f in frames], [P.FINAL, P.READY])
        self.assertGreaterEqual(p.crc_errors, 1)


class ServerTest(unittest.TestCase):
    def test_end_to_end_stdio(self):
        with tempfile.TemporaryDirectory() as d:
            eng = {"engines": [{"name": "fake", "cmd": "python3 -c \"import wave,sys;w=wave.open(sys.argv[1]);print('frames',w.getnframes())\" {wav}"}]}
            ej = os.path.join(d, "e.json")
            json.dump(eng, open(ej, "w"))
            pcm = b"\x01\x00" * 16000                  # 1 s of audio
            req = P.encode(P.HELLO) + P.encode(P.AUDIO_BEGIN, struct.pack("<I8s", 16000, b"en"))
            for i in range(0, len(pcm), 4000):
                req += P.encode(P.AUDIO_CHUNK, pcm[i:i + 4000])
            req += P.encode(P.AUDIO_END) + P.encode(P.SHUTDOWN)
            out = subprocess.run([sys.executable, os.path.join(HERE, "stt_server.py"), "--stdio", "--engines", ej,
                                  "--engine", "fake"], input=req, capture_output=True, timeout=60).stdout
            frames = P.Parser().feed(out)
            types = [f[0] for f in frames]
            self.assertEqual(types, [P.READY, P.READY, P.FINAL])
            text = frames[-1][1][2:].decode()
            self.assertEqual(text, "frames 16000")


if __name__ == "__main__":
    unittest.main()
