# Tab5 CSI Detector Multitool

A **portable, device-free WiFi sensing kit** — detect human presence/motion from
the way a body perturbs **WiFi Channel State Information (CSI)**, no camera,
**passive** by default (no mandatory association to any target network).

> **Scope & ethics (read [`docs/scope-and-safety.md`](docs/scope-and-safety.md)).**
> DIY security-research / skills project, developed and tested **only on Gab's own
> hardware and network**. Passive sensing is the default and is always safe.
> Traffic generation (probe/ping) is opt-in; **deauthentication frames are a hard
> red line — never, even in personal dev.** Any future client use happens only
> under explicit, documented authorization.

## Direction (updated 2026-07-08 — supersedes the earlier "Tab5-centric" note)

The CardputerZero is a Raspberry Pi CM0 (full Linux), **not** an ESP32 — so it
runs the **Nexmon** CSI path, not the Espressif API. The project is now a
**two-node sensing kit**, with the Nexmon feasibility explicitly gated by a
kernel-compatibility check before we invest in it:

| Device | Chip | Role |
|--------|------|------|
| **CardputerZero** | RPi CM0 (BCM2710A1 quad A53, Linux) + WiFi likely **BCM43436B0** (Pi Zero 2 W family) | **Principal sensing node** — Nexmon CSI in monitor mode (passive, no association) and/or standalone AP |
| **Tab5** | ESP32-P4 (RISC-V, no radio) + **ESP32-C6** (WiFi 6, via SDIO) | **Processing hub + 5" display**, and/or **secondary CSI node** (C6, Espressif API) |
| **OnePlus 12** | LTE/5G | **Optional** internet uplink (hotspot) for cloud logging / remote debug — not needed for the sensing loop itself |

```
OnePlus 12 (LTE/5G, hotspot)
       │ optional — internet uplink only
       ▼
CardputerZero (Linux, Nexmon CSI)          principal sensing node
   AP and/or STA · monitor-mode capture (passive) or probe/ping (opt-in)
       │  own local WiFi (the kit's network)
       ▼
Tab5 (ESP32-P4 + C6)                        hub · dashboard · secondary CSI
   processing + classification + 5" display; optional complementary C6 capture
```

The sensing loop (CardputerZero ↔ Tab5) needs **no internet** — a fully mobile,
self-contained kit that touches no existing WiFi at the site.

This project reuses ideas/CSI code from
[`skizzophrenic/Cardputer-CSI-Human-Detector`](https://github.com/skizzophrenic/Cardputer-CSI-Human-Detector)
(MIT, attribution in [LICENSE](LICENSE)).

## The two CSI implementation families

CSI can't be read the same way on both chips — this is the core technical split:

- **Espressif (ESP32-C6 / -S3)** — `esp_wifi_set_csi_config()` + a CSI callback,
  via ESP-IDF. Runs on the **Tab5's C6** and on a plain ESP32-S3 reference node.
  ⚠️ Known C6 bug (subcarrier order / L-LTF in the CSI callback):
  espressif/esp-idf **#14271** — verify it's fixed in the target IDF before
  building the pipeline on C6. Chip CSI quality ranking: C5 > C6 > C3 ≈ S3 > ESP32.
- **Broadcom/Cypress (CardputerZero)** — the **Nexmon** firmware patch enables
  monitor mode (`nexutil -m2`), injection, and native CSI on Linux **without
  association**. The likely chip **BCM43436B0** is in the Nexmon table but is
  **kernel-version-picky** (patch targets ~5.10; failures reported on 6.12).
  See [`research/nexmon-cardputerzero.md`](research/nexmon-cardputerzero.md).

> **Critical-path risk:** the whole "CardputerZero as principal node" plan hinges
> on Nexmon working on the shipped kernel. **First action on receipt:** check the
> M5Stack image's exact kernel vs. the Nexmon compatibility table before investing.

## Passive vs. active

- **Passive (default, always safe):** capture the AP's natural beacons (~10/s).
  Enough to validate the whole processing chain.
- **Traffic generation (opt-in, to densify data):** active probe requests, normal
  ICMP ping. Kept behind an explicit, documented switch in code.
- **Deauth frames: never.** That crosses into active disruption — a different
  legal category. Hard red line, enforced by code structure. See scope-and-safety.

## Layout

```
├── shared/radar_protocol.h     # binary CSI-result wire contract (node → Tab5)
├── firmware-cardputer/         # PlatformIO · ESP32-S3 · reference CSI pipeline (Prototype 1)
├── firmware-tab5/              # PlatformIO · ESP32-P4 · hub/dashboard + C6 secondary CSI
├── hub-cardputerzero/          # Linux (Nexmon) principal sensing node — built on hardware receipt
├── docs/
│   ├── scope-and-safety.md     # authorization scope, guardrails, passive/active separation
│   ├── requirements.md         # sub-needs, decisions, direction history
│   ├── protocol-spec.md        # node → Tab5 data protocol
│   ├── hardware-notes.md       # per-device notes, keyboard options, compute comparison
│   └── enclosure-notes.md      # portable/clamshell enclosure notes (deferred)
└── research/
    ├── nexmon-cardputerzero.md # Nexmon on the CM0 (BCM43436B0), kernel-compat gate
    └── tab5-c6-csi.md          # ESP32-C6 CSI notes + the #14271 bug
```

## Roadmap (concrete next steps)

| # | Step | Depends on |
|---|------|------------|
| 1 | Verify esp-idf **#14271** (C6 CSI) status on the target IDF version | — |
| 2 | On CardputerZero receipt: identify exact WiFi chip (`dmesg`/`lsusb`) + kernel; cross-check Nexmon table | hardware |
| 3 | Clone/study [`espressif/esp-csi`](https://github.com/espressif/esp-csi) as the Tab5/ESP32 pipeline reference | — |
| 4 | Clone/study [`seemoo-lab/nexmon`](https://github.com/seemoo-lab/nexmon) (`patches/bcm43436b0/...`) for the CM0 pipeline | — |
| 5 | **Prototype 1** — basic CSI pipeline on a pure **ESP32-S3** (most proven), before porting to C6 | S3 in hand |
| 6 | **Prototype 2** — Nexmon monitor mode on the CardputerZero, capture frames without association | hardware |
| 7 | **Integration** — local CardputerZero ↔ Tab5 network; classifier (k-NN / SVM baseline) for presence/motion | both |
| 8 | **Logging discipline in code from day one** — scope, date, channel, duration per capture | — |

Steps 1, 3, 4, 5 are doable now; 2, 6, 7 wait on the CardputerZero.

## Sources

- https://github.com/espressif/esp-csi
- https://github.com/seemoo-lab/nexmon
- https://github.com/espressif/esp-idf/issues/14271
- https://docs.espressif.com/projects/esp-idf (Wi-Fi / CSI, ESP32-C6)
