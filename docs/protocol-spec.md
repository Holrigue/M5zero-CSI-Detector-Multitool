# Communication protocol

Canonical, versioned description of the links between the three devices. The
binary Cardputer→hub frame is defined once, in code, in
[`../shared/radar_protocol.h`](../shared/radar_protocol.h) — that header is the
source of truth; this document explains it.

```
Cardputer ──(A) binary RadarPacket──▶ CardputerZero ──(B) JSON──▶ Tab5
   ▲                                                                 │
   └──────────────(C) command channel ◀──────────────────────────────┘
```

During Phase 5 (no hub yet) the Cardputer talks straight to the Tab5 over link
**A** and the Tab5 speaks link **C** directly back — the hub is transparent.

---

## Link A — Cardputer → hub (binary `RadarPacket`)

Fixed-layout, self-framing binary. **~10–20 Hz.** Little-endian on the wire
(native to ESP32 and the RPi CM0). `#pragma pack(1)` — no padding, `sizeof`
equals wire size.

### Frame

```
┌──────┬──────┬───────────── payload (len bytes) ─────────────┬────────┐
│ sync │ len  │ header(12) │ blips[blip_count] (12 B each)     │ crc16  │
│ 0xAA │ u8   │            │                                   │ u16    │
└──────┴──────┴───────────────────────────────────────────────┴────────┘
         └── len = 12 + blip_count*12 ──┘
   CRC and len cover ONLY the payload (from timestamp_ms through the last blip).
```

### Header (12 bytes, after `sync`/`len`)

| Field | Type | Bytes | Meaning |
|-------|------|-------|---------|
| `timestamp_ms` | `uint32` | 4 | sensor `millis()` at capture |
| `calib_state` | `uint8` | 1 | 0 idle · 1 calibrating · 2 ready |
| `motion_intensity` | `float32` | 4 | 0..1 overall CSI motion energy |
| `blip_count` | `uint8` | 1 | valid entries in `blips[]` (0..6) |
| `proto_ver` | `uint8` | 1 | protocol version (currently 1) |
| `reserved` | `uint8` | 1 | 0, reserved for flags |

### Blip (12 bytes each, `RADAR_MAX_BLIPS = 6`)

| Field | Type | Meaning |
|-------|------|---------|
| `angle_deg` | `float32` | scope bearing, 0 = ahead |
| `distance_est` | `float32` | 0 (near) .. 1 (scope edge) |
| `confidence` | `float32` | 0..1 |

### Framing rules

- Every frame starts with `sync = 0xAA`. A reader resynchronises by scanning for
  `0xAA` and validating `len` + CRC.
- `len` = `RADAR_PAYLOAD_BYTES(blip_count)` = `12 + blip_count*12`. Reject any
  frame whose `len` doesn't match a legal `blip_count` (0..6) → 12, 24, …, 84.
- `crc16` = CRC-16/CCITT-FALSE (poly `0x1021`, init `0xFFFF`, no reflection,
  xorout `0x0000`) over exactly the `len` payload bytes. See `radar_crc16()`.
- Max frame = `RADAR_FRAME_MAX` = 2 + 84 + 2 = **88 bytes**. Size a fixed rx
  buffer to this.
- Baud: **115200** to start (`SERIAL_8N1`). Bump later if the CM0 is happy — the
  format doesn't care.

### Semantics — what the numbers actually mean (read this)

Single-antenna WiFi CSI on one ESP32 gives **no reliable bearing and no metric
range**. The reference project acknowledges this: bearing is fixed at 0 and the
"radar" is really a motion-energy display. So in Track A today:

- `motion_intensity` is the honest primary signal — normalized CSI
  amplitude+phase variance (60/40 blend, from the reference sensing core).
- A single blip is synthesized at `angle_deg = 0` when presence is held, with
  `distance_est` derived *relatively* from motion (more motion → drawn nearer)
  and `confidence = motion_intensity`. It is a **visualization**, not a
  triangulated position.

The multi-blip array exists so a future multi-antenna rig or a Nexmon-based
sensor (Track B) can populate real angles **without changing the wire format**.
Don't read metric meaning into `distance_est` until such a sensor exists.

---

## Link B — hub → Tab5 (JSON)

Once the CardputerZero is doing parsing/aggregation, downstream can be simple.
Start with newline-delimited JSON for dev speed; drop to binary only if latency
demands it. Proposed shape (hub owns this — not yet frozen):

```json
{"t":123456,"calib":2,"motion":0.42,"blips":[{"a":0.0,"d":0.6,"c":0.42}]}
```

One object per line, ~10–20 Hz. The Tab5 rendering code is written against this
shape and against link A directly (Phase-5 `-DRADAR_DIRECT`), so both paths feed
the same scope.

---

## Link C — Tab5 → hub (commands)

Touch interactions on the Tab5 send commands upstream (calibration, mode, tuning).
ASCII, newline-terminated, mirrors the reference project's command verbs so the
hub can relay them to the Cardputer unchanged:

| Command | Meaning |
|---------|---------|
| `CAL` | begin calibration (baseline capture) |
| `CALSTOP` | end calibration |
| `THR <float>` | set motion threshold |
| `RATE <hz>` | set packet rate |
| `MODE <name>` | switch view/mode |
| `PING` | liveness check → `PONG` |

In Phase 5 the Cardputer parses these directly; from Phase 6 the hub is the
middleman and may translate them.

---

## Versioning

`proto_ver` (currently `RADAR_PROTO_VER = 1`) rides in every packet. Any change
that alters field layout, sizes, or endianness **must** bump it, and readers must
reject versions they don't understand rather than misparse. Additive-only changes
(using `reserved`, appending after existing blips) can keep the version if old
readers still parse correctly.
