# Requirements — the sub-needs & direction history

This doc records each capability, the decisions taken, an honest feasibility
read, and where it lands on the roadmap. Direction has evolved; the **current**
one is at the top, earlier ones kept for context.

## ⭐⭐ CURRENT direction (2026-07-08, latest): build on RuView, single-node

After reviewing the most mature project in this space
([Ragnar/RuSense](https://github.com/PierreGode/Ragnar), powered by
[`ruvnet/ruview`](https://github.com/ruvnet/ruview), both MIT), we **build on
RuView** instead of reinventing the CSI pipeline. Full analysis:
[`../research/prior-art-ruview.md`](../research/prior-art-ruview.md).

- **CSI sensor = a single ESP32-S3** running RuView's node firmware (UDP CSI).
- **Brain = CardputerZero** running RuView's Rust sensing-server (inference +
  REST/WS API). Its Linux compute is finally the right job for it.
- **Tab5 = a native dashboard client** of the RuView API (presence/motion/vitals/
  heatmap on the 5" screen + touch).
- **Single-node first;** multi-node ESP32 mesh (fusion → people-count/positioning/
  pose) is **optional Phase 2** (one chip type only — never mix S3/C6).
- **Nexmon on the CardputerZero is no longer needed** for the main path (the Pi is
  the server, not the CSI source) → demoted to an optional experiment. This
  answers the previously-open "CSI source" question (→ ESP32-S3 node).
- Our own `shared/` + `firmware-*` code becomes the **offline no-server fallback**
  (direct ESP32→Tab5 UART), kept as a hardware-minimal mode + learning artifact.

This supersedes the "Tab5-centric" note below where they conflict (the Tab5 is now
a client of a Pi-hosted server, not necessarily fully standalone). The Tab5's
role-adaptive / keyboard work still applies to the client app.

## ⭐ Earlier direction (2026-07-08): Tab5-centric

After weighing complexity and the compute reality (the Tab5's ESP32-P4 is a
capable display MCU; the CardputerZero is a more powerful Linux computer but adds
a whole OS/hub layer and its own CSI dead-end), the project pivots to a
**single-device, Tab5-centric design**:

- **The Tab5 is the whole device** — brain + display + touch + **keyboard**, all
  firmware. It runs standalone on hardware in hand.
- **Keyboard**: add the **official Tab5 Keyboard** (70-key, I2C via Ext.Port1,
  plug-and-play, `M5Unit-KEYBOARD` lib). This resolves need #3 without any custom
  hardware. I2C leaves the Grove-UART free for a sensor.
- **CardputerZero → optional future upgrade**, not a required part. Bring it in
  later only for Linux logging / advanced calibration / light ML, or as the brain
  when the Tab5 acts as its 2nd screen (needs #2b/#5 secondary mode). Everything
  core works without it.
- **CSI source — the one still-open decision** (needs hardware to settle):
  1. the **Tab5's own ESP32-C6** (elegant, single device — but CSI must be
     exposed through `esp_wifi_remote`; unproven, see `../research/tab5-c6-csi.md`), or
  2. a **small dedicated ESP32 node** wired over Grove-UART (guaranteed; the
     `firmware-cardputer` code already does exactly this). One extra ~$10 board.

The sections below keep the original per-need analysis; read them through the
lens of this pivot (the CardputerZero-dependent parts are now "optional/later").

Terminology: **sensor** = an ESP32 CSI node (Tab5's C6, or a dedicated ESP32),
**hub** = CardputerZero (RPi CM0, Linux, optional), **display/brain** = Tab5.

---

## 1. CSI radar, with the CardputerZero as the Linux brain

**Decision: CSI capture stays on the ESP32 Cardputer; the CardputerZero is the
Linux brain (logging, calibration, light ML), not the CSI source.**

- The guaranteed CSI path is Espressif's proprietary API, ESP32-only. That runs
  on the Cardputer and streams `RadarPacket`s to the hub.
- The hub adds what Linux is good at: session logging, advanced/persisted
  calibration and presets, and eventually light ML — all downstream of the raw
  sensing.
- CSI *on the Zero itself* (Nexmon on the Cypress CYW43459) stays an exploratory
  bonus (Track B) — no known patch, possible reverse engineering, blocked on
  hardware. It must never gate this need. See
  [`../research/nexmon-cardputerzero.md`](../research/nexmon-cardputerzero.md).

**Maps to:** phases 1–2 (sensor, done), 5 (direct proof), 6 (insert hub).

---

## 2. Tab5 as an extension / second screen of the Cardputer

**Decision: both, phased — the dedicated application screen first (works with
hardware in hand), a generic Linux second monitor later.**

- **2a — dedicated app screen (now):** the Tab5 renders the radar / project UI
  pushed from the sensor or hub over the data link. Already scaffolded
  (`firmware-tab5`). No new hardware needed.
- **2b — generic Linux second monitor (later):** drag arbitrary windows of the
  Zero's desktop onto the Tab5. The Tab5 (ESP32-P4, MIPI-DSI panel) is not a
  DP/HDMI monitor, so this needs a **framebuffer-streaming client** on the Tab5
  plus an emitter on the Zero (VNC / spacedesk-style). Practical transport is
  **WiFi**, not the Tab5's USB 2.0. A real sub-project; blocked on the hub.

**Maps to:** phases 3–4 (2a, done), a new phase (2b, blocked on hub). Shares
streaming tech with need 4.

---

## 3. Cardputer keyboard usable on the Tab5 (and the Zero)

**Feasible and clean.** The Cardputer has a physical keyboard; the Tab5 does not.
When the stack is connected, Cardputer keypresses are routed to whichever device
has focus.

- **To the hub (Zero):** the Cardputer's ESP32-S3 can enumerate over USB as a
  **real USB-HID keyboard** — plug it into the CardputerZero and it is a native
  Linux keyboard, no bridging. Strong, low-risk path.
- **To the Tab5:** forward key events over the existing data link as input
  events. This needs a small protocol addition (a message type distinct from
  `RadarPacket`) — see the "Planned: input events" note in
  [`protocol-spec.md`](protocol-spec.md). Design only for now; not yet on the wire.

**Maps to:** a new phase (firmware-develop the HID + event forwarding; final
validation needs the hub/stack).

---

## 4. (Optional) Tab5 as a portable display for an external USB-C signal

**Honest ceiling: not plug-and-play.** The Tab5's USB-C is USB 2.0 (P4 OTG) with
**no DisplayPort Alt-Mode**, and its MIPI-DSI panel has no video-capture front
end. You cannot plug an HDMI/DP source and see it.

- **Achievable version:** a "network/USB monitor" where the *source* streams
  pixels to the Tab5 (DisplayLink/spacedesk model) — requires software on the
  source, and USB 2.0 caps full-res 1280×720@60. Shares the client with need 2b.
- **True external-display-in** would require an added DP→MIPI bridge board
  (hardware not on the Tab5 today).

**Maps to:** stretch/optional; revisit after 2b, since they share the streaming
client.

---

## 5. Role-adaptive Tab5: standalone radar, auto-switch to 2nd screen with the Zero

**Decision: support both, with auto-selection.** The Tab5 adapts its role to
whatever hardware is present, choosing its sensing source and brain automatically.

Device recap (so the compute model is unambiguous): the Cardputer (ESP32-S3) and
the **Tab5 (ESP32-P4 + ESP32-C6 WiFi)** run *firmware*; the **CardputerZero is a
Raspberry Pi CM0 running Linux** — normal Linux software, no firmware, never a
CSI source in Track A.

### Modes (auto-selected at runtime)

| Present | Brain | CSI source | Tab5 role |
|---------|-------|-----------|-----------|
| Tab5 only, C6 CSI works | Tab5 | Tab5's own ESP32-C6 | **PRIMARY** — self-contained radar |
| Tab5 + Cardputer, no Zero | Tab5 | Cardputer over the link | **PRIMARY** — renders remote sensing |
| Tab5 + CardputerZero | **Zero (Linux)** | Cardputer (via hub) | **SECONDARY_DISPLAY** — full-size scan map |

### Selection priority

1. **Zero connected?** (hub handshake/heartbeat seen on the USB-C/UART link) →
   `SECONDARY_DISPLAY`: the Zero is the brain and pushes the full-size scanning
   map; the Tab5 just renders (and sends touch/commands back).
2. else **C6 self-CSI available?** → `PRIMARY`, sensing on the Tab5's own C6.
3. else **Cardputer sensor stream on the link?** → `PRIMARY`, rendering that.
4. else → `NO SOURCE` banner.

### The one real unknown — Tab5 self-CSI (the "if possible" part)

The P4 has no radio; WiFi is the ESP32-C6 co-processor over esp-hosted. The C6
supports the CSI API in principle, but **CSI must be exposed through
`esp_wifi_remote`/esp-hosted** — historically limited. This gates mode 1 only.
Tracked as a spike in [`../research/tab5-c6-csi.md`](../research/tab5-c6-csi.md);
until it's proven, the Tab5 still delivers a standalone radar via the Cardputer
(mode 2) and the 2nd-screen mode with the Zero (mode 3).

### Auto-switch mechanism

A lightweight hub **handshake** on the link: the Zero periodically announces
itself; the Tab5's role state machine flips to `SECONDARY_DISPLAY` on detect and
back to `PRIMARY` on timeout. Same idea works over USB-C (USB-CDC serial) or
WiFi. No user action — plug in the Zero and it becomes the brain.

**Maps to:** Tab5 role state machine + hub handshake (developable now, self-CSI
part gated on the spike); `SECONDARY_DISPLAY` full-size map validated once the
Zero arrives.

---

## Consolidated roadmap position

| Need | Now (hardware in hand) | Later (needs hub / hardware) |
|------|------------------------|------------------------------|
| 1 CSI radar + Linux brain | ESP32 sensing + direct link (ph 1–2, 5) | hub logic/logging (ph 6) |
| 2 Tab5 second screen | 2a dedicated app screen (ph 3–4) | 2b generic Linux monitor |
| 3 Keyboard bridge | develop HID + event forwarding firmware | validate on the stack |
| 4 External display | — | optional streamed monitor |
| 5 Role-adaptive Tab5 | role state machine + Cardputer-fed PRIMARY; C6 self-CSI spike | Zero handshake → SECONDARY_DISPLAY full-size map |

Guiding rule, unchanged: nothing on the "Now" column depends on Nexmon or on the
CardputerZero. Everything achievable today runs on the Cardputer + Tab5 already
in hand.
