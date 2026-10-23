# Nomiq Air Protocol — v1 (draft)

Reference implementation: `firmware/components/nomiq_proto/` (host-testable C). This document is normative; if code and doc disagree, open an issue.

## 1. Frame format

Max LoRa PHY payload 255 B. All multi-byte fields **little-endian**.

```
 0        1        2        3        4               8               12      14      16      18      20
+--------+--------+--------+--------+---------------+---------------+-------+-------+-------+-------+----------------+--------+
| ver    | type   | flags  | len    | src (u32)     | dst (u32)     |msg_id |frag_ix|frag_n | crc16 | payload (len)  | MIC(8) |
+--------+--------+--------+--------+---------------+---------------+-------+-------+-------+-------+----------------+--------+
```

| Field | Size | Description |
|---|---|---|
| `ver` | 1 | Protocol version = `1` |
| `type` | 1 | See §2 |
| `flags` | 1 | b0-1 priority (0 = text/control … 3 = bulk), b2 ACK requested, b3 retransmission, b4 encrypted, b5-7 reserved |
| `len` | 1 | Payload length, 0–227 |
| `src` / `dst` | 4 / 4 | Device IDs (lower 32 bits of eFuse MAC hash). `0xFFFFFFFF` = broadcast (BEACON only) |
| `msg_id` | 2 | Per-sender message counter (wraps; disambiguated by `src` + key epoch) |
| `frag_ix` | 2 | Fragment index (0-based). For SACK = window base |
| `frag_n` | 2 | Total fragments of the object; 1 for single-frame messages |
| `crc16` | 2 | CRC-16/CCITT-FALSE over header bytes 0–17 + payload. Detects corruption cheaply before crypto |
| `MIC` | 8 | AES-128-CCM tag (present when flags.b4 set) |

Header = **20 B**, max payload per fragment = 255 − 20 − 8 = **227 B**.

## 2. Message types

| Code | Name | Direction | Payload |
|---|---|---|---|
| 0x01 | TEXT | A→B | UTF-8 text ≤ 227 B (longer text is sent as an object) |
| 0x02 | OFFER | A→B | `obj_type u8` (text/voice/photo), `size u32`, `sha256[0:8]`, `codec/format u8`, `meta…` |
| 0x03 | ACCEPT | B→A | resume bitmap (fragments already held), optional data-channel |
| 0x04 | DATA | A→B | fragment bytes |
| 0x05 | SACK | B→A | 64-bit bitmap of window starting at `frag_ix`, `snr i8`, `rssi i8` |
| 0x06 | DONE | B→A | object verified & stored |
| 0x07 | CANCEL | either | reason u8 |
| 0x08 | REJECT | B→A | reason: storage full / too big / unknown sender |
| 0x09 | BEACON | broadcast | name, battery, capabilities (discovery) |
| 0x0A | PAIR | A↔B | X25519 public key + nonce (pairing) |
| 0x0B | ACK | B→A | ACK for TEXT / control frames |

## 3. Reliability

* **ACK/retry:** TEXT and control frames retry with exponential backoff + jitter: 1, 2, 4, 8 × (ToA + 200 ms), max 5 tries.
* **Windowed selective-repeat:** sender transmits up to 16 DATA frames, then waits for SACK. Only missing fragments are resent. Window base advances to the first missing fragment.
* **Duplicate filter:** receiver keeps an LRU of the last 64 `(src, msg_id, frag_ix, type)` tuples; duplicates are re-ACKed but not re-processed.
* **Integrity:** per-frame CRC16 + MIC; per-object truncated SHA-256 in OFFER checked after reassembly. Mismatch → discard and request full resend.
* **Timeouts:** receiver drops an incomplete object after 30 min idle (configurable) but **keeps fragments on SD** so the sender can resume.
* **Resume:** both sides persist the bitmap on SD. A new OFFER with the same `(src, sha)` gets an ACCEPT carrying the bitmap of fragments already held.
* **Priority:** TX scheduler services queues strictly: control > text > voice > photo. A bulk transfer yields after the current window if text is queued.

## 4. Security

* **Pairing (TOFU + confirmation):** devices exchange X25519 public keys in PAIR frames; both screens show a 6-digit code derived from the shared secret; users confirm. Keys stored in encrypted NVS.
* **Per-pair key:** `K = HKDF-SHA256(ECDH, "nomiq v1" ‖ min(id) ‖ max(id))`, AES-128-CCM, 8-byte tag.
* **Nonce (13 B):** `src(4) ‖ key_epoch(4) ‖ msg_id(2) ‖ frag_ix(2) ‖ type(1)`. `key_epoch` increments on every boot (persisted) so `msg_id` wrap never reuses a nonce.
* Header is authenticated (AAD), payload encrypted. BEACON is unencrypted but signed-later (v2).
* Unpaired senders → REJECT (configurable "open mode" for demos).

## 5. State machines

Sender: `PENDING → OFFERED → SENDING(window) ↔ WAIT_SACK → WAIT_DONE → DELIVERED` (+ `FAILED`, `CANCELLED`, `PAUSED` for duty-cycle wait or user pause).
Receiver: `IDLE → RECEIVING → VERIFY → STORED`, `PARTIAL` (persisted, resumable).

## 6. Versioning

Unknown `ver` → drop silently. New optional fields go in payload TLVs; header layout is frozen for v1.
