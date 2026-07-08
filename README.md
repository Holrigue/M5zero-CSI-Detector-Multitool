# M5Zero CSI Detector Multitool

A portable WiFi **Channel State Information (CSI)** human-presence radar —
**Tab5-centric**, with the CardputerZero as an optional future upgrade.

> **Direction (decided 2026-07-08): Tab5-centric.** The **Tab5 is the whole
> device** — brain + display + touch + keyboard, all firmware. It runs standalone
> on hardware in hand plus an off-the-shelf keyboard. The **CardputerZero is now
> an *optional future upgrade***, not a required part. See
> [`docs/requirements.md`](docs/requirements.md) for the full rationale.

| Role | Device | Chip | Status |
|------|--------|------|--------|
| **Brain + display + touch + keyboard** | Tab5 | ESP32-P4 (+C6) | ✅ in hand (keyboard: add the official Tab5 Keyboard, I2C) |
| **CSI sensor** | Tab5's own C6 *or* a small dedicated ESP32 node | ESP32-C6 / ESP32 | ⚠️ **source not yet decided** (see below) |
| **Optional future hub** | CardputerZero | RPi CM0 (Linux) | ⏳ optional, ~Nov 2026 |

```
                 ┌───────────────────────── Tab5 (ESP32-P4) ─────────────────────────┐
   CSI source ──▶│  app firmware: brain + scope render + touch + keyboard (I2C)       │
  (C6 or ESP32)  └───────────────────────────────────────────────────────────────────┘
                        ▲ Grove-UART (if external ESP32 sensor)   ▲ I2C (Tab5 Keyboard)
   ( optional, later )  CardputerZero — Linux logging / ML / Nexmon experiments
```

This is a multi-device adaptation of
[`skizzophrenic/Cardputer-CSI-Human-Detector`](https://github.com/skizzophrenic/Cardputer-CSI-Human-Detector)
(originally an all-in-one Cardputer + wired ILI9341 panel). See [LICENSE](LICENSE)
for attribution — the CSI sensing core is derived from that project, MIT-licensed.

## The one constraint to never forget

CSI extraction uses Espressif's **proprietary** API (`esp_wifi_set_csi_rx_cb`),
which only exists on **ESP32** WiFi chips. So the CSI source must be an ESP32:
either the **Tab5's own ESP32-C6** (needs CSI to be exposed through
`esp_wifi_remote` — unproven, see [`research/tab5-c6-csi.md`](research/tab5-c6-csi.md))
or a **small dedicated ESP32 node** wired to the Tab5 over Grove-UART (guaranteed).
The CardputerZero's WiFi has **no working CSI path** (Nexmon is exploratory —
[`research/nexmon-cyw43459.md`](research/nexmon-cyw43459.md)) and is never relied on
for sensing.

> **Open decision:** C6 self-sensing (elegant, unproven) vs. a dedicated ESP32
> sensor (guaranteed, one extra small board). To resolve with hardware in hand.

## Paths

- **Primary — Tab5-centric** (this is the project): the Tab5 runs the whole app
  as firmware. CSI comes from an ESP32 source (its own C6, or a small dedicated
  ESP32 node over Grove-UART, carrying binary
  [`RadarPacket`](shared/radar_protocol.h)s); the Tab5 renders the scope and
  takes touch + keyboard input.
- **Optional future — CardputerZero hub**: add the Linux CM0 later for logging,
  advanced calibration, light ML, and as the brain when you want the Tab5 to act
  as its 2nd screen. Never required; the Tab5-centric device works without it.
- **Exploratory — Nexmon on the CardputerZero**: a background research bet, never
  a prerequisite.

## Layout

```
radar-multi/
├── shared/radar_protocol.h     # canonical binary wire contract (both ends #include this)
├── firmware-cardputer/         # PlatformIO · ESP32 · CSI sensing + UART tx (dedicated-sensor option)
├── firmware-tab5/              # PlatformIO · ESP32-P4 · app: scope + touch + keyboard + role SM
├── hub-cardputerzero/          # Linux hub — OPTIONAL future upgrade (README only)
├── docs/
│   ├── requirements.md         # the sub-needs, decisions, and the Tab5-centric pivot
│   ├── protocol-spec.md        # detailed wire protocol
│   ├── hardware-notes.md       # pinouts, keyboard options, compute comparison
│   └── enclosure-notes.md      # Tab5 clamshell / hinge notes (deferred)
└── research/
    ├── tab5-c6-csi.md          # spike: CSI on the Tab5's own ESP32-C6
    └── nexmon-cyw43459.md      # exploratory: Nexmon on the CardputerZero
```

## Roadmap

See [`docs/requirements.md`](docs/requirements.md) for the five sub-needs, the
decisions, and the Tab5-centric pivot. Compute model (don't conflate): the ESP32
sensor node and the **Tab5 (ESP32-P4 + ESP32-C6)** run *firmware*; the **optional
CardputerZero is a Raspberry Pi CM0 running Linux** — never a CSI source.

**Core — Tab5-centric, buildable with hardware in hand:**

| Phase | What | Need | State |
|-------|------|------|-------|
| 1–2 | ESP32 sensor node: CSI sensing intact, `uart_tx.h` binary packet stream | 1 | ✅ done |
| 3–4 | Tab5: scope render + touch interactions | 2a | ✅ done |
| 5 | Direct ESP32-sensor ↔ Tab5 UART link — end-to-end proof | 1 | ⚙️ ready to bench-test |
| 6 | Tab5 role state machine (standalone PRIMARY, hub-handshake hook) | 5 | ✅ done |
| 7 | **Decide CSI source** — Tab5 C6 spike vs dedicated ESP32 node | 1 | ⏳ needs hw |
| 8 | Tab5 keyboard input (official Tab5 Keyboard, I2C) → app controls | 3 | ⏳ needs keyboard |
| 9 | C6 self-CSI spike → Tab5 fully standalone (no external sensor) | 5 | 🔬 needs hw |

**Optional / later (CardputerZero, printer):**

| Phase | What | Need | State |
|-------|------|------|-------|
| 10 | Add CardputerZero hub: Linux logging / calibration / light ML | 1 | ⛔ optional, ~Nov 2026 |
| 11 | Zero as brain → Tab5 auto-switches to SECONDARY_DISPLAY (full-size map) | 5,2a | ⛔ optional |
| 12 | Tab5 as a generic Linux second monitor (framebuffer over WiFi) | 2b | ⛔ optional |
| 13 | Tab5 as a streamed portable display for an external source | 4 | ⛔ optional |
| 14 | Nexmon CSI experiments on the CardputerZero | 1(bonus) | ⛔ exploratory |
| 15 | 3D-printed Tab5 clamshell enclosure (Bambu P1S) | — | ⛔ printer, summer 2026 |

The core path needs only the Tab5 (+ a cheap keyboard, + an ESP32 sensor unless
the C6 spike pans out) — no CardputerZero required.

## Quick start

Each firmware is an independent PlatformIO project:

```bash
# Sensor (ESP32-S3)
cd firmware-cardputer
cp credentials.ini.example credentials.ini   # add your home WiFi
pio run -e cardputer-radar-csi -t upload -t monitor

# Display (ESP32-P4)
cd firmware-tab5
pio run -e tab5 -t upload -t monitor
```

For a hub-less Phase-5 bench test, wire Cardputer Grove UART straight to a Tab5
UART (see [`docs/hardware-notes.md`](docs/hardware-notes.md)) and build the Tab5
with `-DRADAR_DIRECT=1`.
