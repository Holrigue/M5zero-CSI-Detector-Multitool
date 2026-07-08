# hub-cardputerzero — OPTIONAL future upgrade (not core)

**Status: optional, and not implemented yet.** As of the 2026-07-08 Tab5-centric
pivot (see [`../docs/requirements.md`](../docs/requirements.md)), the project's
core device is the Tab5 alone — the CardputerZero is **no longer a required
part**. Bring it in later only for Linux-side extras: session logging, advanced
calibration/presets, light ML, or acting as the brain when the Tab5 is used as a
2nd screen.

Still not built until the CardputerZero (RPi CM0) is in hand (~Nov 2026):
doing so now would mean guessing at UART device paths, the `G4_I2C/UART_SW` GPIO
multiplexing, kernel/Python versions, and CM0 throughput — all unknowns that
change the design.

What is *already decided* (so integration is drop-in when the hardware lands):

- **Inbound (from Cardputer):** the binary `RadarPacket` defined in
  [`../shared/radar_protocol.h`](../shared/radar_protocol.h) — same header, same
  CRC-16, same little-endian layout the sensor emits today. The hub just needs a
  Python/Node transcription of that struct + `radar_crc16`.
- **Outbound (to Tab5):** newline-delimited JSON (see
  [`../docs/protocol-spec.md`](../docs/protocol-spec.md) §Link B). The Tab5 render
  code already targets the same `RadarPacket` shape, so the hub's job is
  parse → (log/calibrate/aggregate) → re-emit.
- **Commands (from Tab5):** ASCII verbs (`CAL`, `THR`, `RATE`, …) relayed
  upstream to the Cardputer unchanged (§Link C).

Planned structure once unblocked (Phase 6):

```
hub-cardputerzero/
├── uart_bridge.py       # read RadarPacket from Cardputer UART, relay JSON to Tab5
├── logging/             # SD/file logging of sessions
└── calibration/         # advanced baseline capture / presets, moved off the MCU
```

### First things to verify on the real device (§8)

- Is a native UART reachable without bit-banging the `G4_I2C/UART_SW` mux?
- Measured end-to-end latency Cardputer→Zero→Tab5 — if too high, move a segment
  to local WiFi.
- CM0 comfortable baud ceiling (bump above 115200 if stable).

Until then, Phase 5 runs the Cardputer and Tab5 **directly** over UART with no
hub — see the repo root README.
