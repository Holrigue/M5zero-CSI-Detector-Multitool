# Tab5 CSI Detector Multitool

A **portable, device-free WiFi sensing kit** — detect human presence/motion (and,
with a model, vitals) from how a body perturbs **WiFi Channel State Information
(CSI)**. No camera, **passive** by default.

> **Scope & ethics — read [`docs/scope-and-safety.md`](docs/scope-and-safety.md).**
> DIY security-research project, developed/tested **only on Gab's own hardware and
> network**. Passive sensing is the default. Traffic generation is opt-in;
> **deauth frames are a hard red line — never.** Client use only under explicit,
> documented authorization.

## Direction (updated 2026-07-08 — build on RuView)

Rather than reinvent the CSI pipeline, we **build on
[`ruvnet/ruview`](https://github.com/ruvnet/ruview)** (MIT) — a mature WiFi-CSI
sensing engine (ESP32 node firmware + Rust inference server + pretrained models),
the same engine the [Ragnar/RuSense](https://github.com/PierreGode/Ragnar) project
integrates. Full analysis + what we take: [`research/prior-art-ruview.md`](research/prior-art-ruview.md).

**Single-node first.** Multi-node ESP32 mesh (fusion for people-count/positioning/
pose) is an **optional Phase 2**.

```
1× ESP32-S3 CSI node (RuView firmware)
        │ CSI over UDP, on the kit's own 2.4 GHz WiFi
        ▼
CardputerZero (Linux)  →  runs the RuView sensing-server (arm64 Docker / binary)
        │ REST /api/v1/* + WebSocket (:3000), over the kit's local WiFi
        ▼
Tab5 (ESP32-P4 + C6)   →  native dashboard client of the RuView API:
        presence / motion / vitals / heatmap on the 5" screen + touch
   (optional: an Android phone hotspot for internet uplink — not needed for sensing)
```

| Device | Role |
|--------|------|
| **ESP32-S3** (1×) | CSI **sensor node** — RuView firmware, streams CSI over UDP. Cheap (~$9), the most-proven CSI chip. |
| **CardputerZero** (RPi CM0, Linux) | **Brain** — runs the RuView sensing-server (inference + REST/WS API). Its strength (Linux compute) is finally the right job for it. |
| **Tab5** (ESP32-P4 + C6) | **Display + touch** — native client of the RuView API on the 5" screen. |
| **Android phone** | *Optional* LTE/5G uplink (cloud log / remote debug). Not needed for the sensing loop. |

Key consequence: the CardputerZero is the **server**, not the CSI source — so
**Nexmon is no longer needed** for the main path (it's now a purely optional
experiment). The earlier "CardputerZero as principal Nexmon node" plan is
superseded.

## Two paths

- **Primary — RuView-based** (above): ESP32-S3 node → CardputerZero (RuView
  server) → Tab5 (RuView client). Best capabilities (presence, motion, vitals,
  and pose/people-count once multi-node). **Dev with no hardware today:**
  `docker run -p 3000:3000 ruvnet/wifi-densepose` serves the API on simulated data.
- **Fallback — our own no-server path**: a direct ESP32→Tab5 UART link using our
  binary [`RadarPacket`](shared/radar_protocol.h) + on-Tab5 rendering (the
  `firmware-*` code + role state machine). Hardware-minimal, no Pi, offline — kept
  as a fallback and learning artifact.

## Built on / credit

- Engine: **[RuView](https://github.com/ruvnet/ruview)** (MIT, © rUv) — CSI
  ingestion, inference, pose/vitals, node firmware, pretrained models.
- Integration reference: **[Ragnar / RuSense](https://github.com/PierreGode/Ragnar)** (MIT).
- Original inspiration: [`skizzophrenic/Cardputer-CSI-Human-Detector`](https://github.com/skizzophrenic/Cardputer-CSI-Human-Detector) (MIT).

See [LICENSE](LICENSE) for attributions.

## Layout

```
├── research/
│   ├── prior-art-ruview.md      # ⭐ what we adopt from RuView/RuSense + how
│   ├── nexmon-cardputerzero.md  # Nexmon — now optional/experimental only
│   └── tab5-c6-csi.md           # ESP32-C6 CSI notes + esp-idf #14271
├── docs/
│   ├── scope-and-safety.md      # authorization scope + guardrails
│   ├── requirements.md          # sub-needs, decisions, direction history
│   ├── protocol-spec.md         # our fallback wire protocol (RadarPacket)
│   ├── hardware-notes.md        # per-device notes, keyboard, compute comparison
│   └── enclosure-notes.md       # portable enclosure (deferred)
├── shared/ · firmware-cardputer/ · firmware-tab5/   # fallback no-server path
└── hub-cardputerzero/           # will host the RuView server setup for the CM0
```

## Roadmap

**Now (no hardware needed):**

| # | Step |
|---|------|
| 1 | Run RuView Docker (simulated data); learn the `/api/v1/*` + WS API |
| 2 | Prototype the **Tab5 RuView client** (M5GFX dashboard: presence/motion/vitals/heatmap) against that API |
| 3 | Verify esp-idf **#14271** (C6 CSI) on the target IDF |

**With hardware in hand:**

| # | Step | Needs |
|---|------|-------|
| 4 | Flash **1× ESP32-S3** with RuView node firmware; provision to the kit WiFi | ESP32-S3 |
| 5 | Run the **RuView server on the CardputerZero** (arm64); confirm the light path (presence/motion/vitals) fits the CM0 | CardputerZero |
| 6 | End-to-end: node → CardputerZero → Tab5 client | all three |
| 7 | Tab5 keyboard input (official Tab5 Keyboard, I2C) → app controls | keyboard |

**Optional / later:**

| # | Step |
|---|------|
| P2 | **Multi-node ESP32 mesh** (fusion: people-count / positioning / pose) — one chip type only |
| — | **M5Paper Color as an optional bedside/glanceable panel** — low-power e-ink status + health trends (a thin RuView client; not a Tab5 replacement — see [hardware-notes](docs/hardware-notes.md)) |
| — | Adaptive training loop, geofence, monitoring modes, notifications (RuSense-style) |
| — | Nexmon experiment on the CardputerZero (extra illuminator) |
| — | 3D-printed portable/clamshell enclosure (Bambu P1S) |

## Sources

- https://github.com/ruvnet/ruview · https://github.com/PierreGode/Ragnar
- https://github.com/espressif/esp-csi · https://github.com/seemoo-lab/nexmon
- https://github.com/espressif/esp-idf/issues/14271
