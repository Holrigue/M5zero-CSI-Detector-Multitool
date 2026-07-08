# radar-multi — CSI Human-Presence Radar, multi-device edition

A portable "human radar" that senses people through walls-of-air using **WiFi
Channel State Information (CSI)**, split across three stacked M5Stack devices:

| Role | Device | Chip | Status |
|------|--------|------|--------|
| **Sensor** | Cardputer / Cardputer-Adv | ESP32-S3 | ✅ in hand |
| **Hub / brain** | CardputerZero | RPi CM0 (Linux) | ⏳ ~Nov 2026 |
| **Display** | Tab5 | ESP32-P4 | ✅ in hand |

```
[Cardputer-Adv ESP32-S3]        [CardputerZero — Linux]         [Tab5 ESP32-P4]
   CSI sensing (native WiFi) ──▶   Hub / logic / logging   ──▶   Scope + touch UI
   built-in screen = status        (Python or Node)              full-screen radar
        │  UART (Grove, binary)          │  UART or WiFi (JSON)
        ▼                                 ▼
```

This is a multi-device adaptation of
[`skizzophrenic/Cardputer-CSI-Human-Detector`](https://github.com/skizzophrenic/Cardputer-CSI-Human-Detector)
(originally an all-in-one Cardputer + wired ILI9341 panel). See [LICENSE](LICENSE)
for attribution — the CSI sensing core is derived from that project, MIT-licensed.

## The one constraint to never forget

CSI extraction on the Cardputer uses Espressif's **proprietary** API
(`esp_wifi_set_csi_rx_cb`), which only exists on ESP32 WiFi chips. The
CardputerZero's Cypress **CYW43459** chip has **no working CSI path today**
(Nexmon CSI *might* be portable to it — unproven, exploratory). So:

> **The whole system is built so that CSI runs on the ESP32 Cardputer.** The
> CardputerZero is a Linux hub, not a required CSI source. Never make the design
> depend on Nexmon working. See [`research/nexmon-cyw43459.md`](research/nexmon-cyw43459.md).

## Two tracks

- **Track A — guaranteed** (built first, no new hardware needed): Cardputer
  senses CSI → sends compact binary [`RadarPacket`](shared/radar_protocol.h)s
  over UART → (hub) → Tab5 renders the scope. This repo implements Track A.
- **Track B — exploratory** (only once the CardputerZero arrives): investigate
  Nexmon CSI on the CYW43459. Bonus, never a prerequisite.

## Layout

```
radar-multi/
├── shared/radar_protocol.h     # canonical binary wire contract (both ends #include this)
├── firmware-cardputer/         # PlatformIO · ESP32-S3 · CSI sensing + UART tx
├── firmware-tab5/              # PlatformIO · ESP32-P4 · M5Unified/M5GFX scope + touch
├── hub-cardputerzero/          # Linux hub — DEFERRED until hardware arrives (README only)
├── docs/
│   ├── protocol-spec.md        # detailed wire protocol
│   └── hardware-notes.md       # pinouts, wiring, per-device notes
└── research/
    └── nexmon-cyw43459.md      # Track B research journal
```

## Roadmap

The work serves five sub-needs (CSI radar + Linux brain · Tab5 second screen ·
Cardputer keyboard shared to the Tab5/Zero · optional USB-C portable display ·
role-adaptive Tab5 that runs standalone and auto-switches to a 2nd screen when
the Zero is plugged in) — see [`docs/requirements.md`](docs/requirements.md) for
the decisions and feasibility of each.

Compute model (don't conflate): the Cardputer (ESP32-S3) and **Tab5 (ESP32-P4 +
ESP32-C6)** run *firmware*; the **CardputerZero is a Raspberry Pi CM0 running
Linux** — normal Linux software, no firmware, never a CSI source.

| Phase | What | Need | Hardware | State |
|-------|------|------|----------|-------|
| 1 | Cardputer: drop `ext_panel.h`, keep CSI sensing intact | 1 | Cardputer | ✅ done |
| 2 | Cardputer: `uart_tx.h`, validate packet stream on a serial monitor | 1 | Cardputer | ✅ done |
| 3 | Tab5: M5Unified/M5GFX skeleton, UART rx, basic scope render | 2a | Tab5 | ✅ done |
| 4 | Tab5: touch interactions (tap blip, calibrate, switch view) | 2a | Tab5 | ✅ done |
| 5 | Direct Cardputer ↔ Tab5 UART link (no hub) — end-to-end proof | 1,2a | both | ⚙️ ready to bench-test |
| 6 | Cardputer keyboard → Tab5 (event forwarding) + USB-HID to a host | 3 | Cardputer(+Tab5) | ⏳ next up |
| 7 | Tab5 role state machine: standalone PRIMARY (Cardputer-fed) + hub handshake | 5 | Tab5(+Cardputer) | ⏳ next up |
| 8 | Spike: CSI on the Tab5's own ESP32-C6 → self-sensing standalone mode | 5 | Tab5 | 🔬 to investigate |
| 9 | Insert CardputerZero hub; move calib/logging to Linux | 1 | CardputerZero | ⛔ blocked on hw |
| 10 | Zero connected → Tab5 auto-switches to SECONDARY_DISPLAY, full-size map | 5,2a | CardputerZero | ⛔ blocked on hw |
| 11 | Tab5 as a generic Linux second monitor (framebuffer stream over WiFi) | 2b | CardputerZero | ⛔ blocked on hw |
| 12 | (Optional) Tab5 as a streamed portable display for an external source | 4 | — | ⛔ optional |
| 13 | (Exploratory) Nexmon CSI for CYW43459 | 1(bonus) | CardputerZero | ⛔ blocked on hw |
| 14 | 3D-printed stacked enclosure (Bambu P1S) | — | printer | ⛔ blocked on hw |

Phases 1–8 need only the hardware already in hand.

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
