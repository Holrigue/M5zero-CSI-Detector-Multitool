# Requirements — the four sub-needs

The project decomposes into four capabilities. This doc records each one, the
decisions taken, an honest feasibility read, and where it lands on the roadmap.
Terminology: **sensor** = Cardputer (ESP32-S3), **hub** = CardputerZero (RPi CM0,
Linux), **display** = Tab5 (ESP32-P4).

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
  [`../research/nexmon-cyw43459.md`](../research/nexmon-cyw43459.md).

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

## Consolidated roadmap position

| Need | Now (hardware in hand) | Later (needs hub / hardware) |
|------|------------------------|------------------------------|
| 1 CSI radar + Linux brain | ESP32 sensing + direct link (ph 1–2, 5) | hub logic/logging (ph 6) |
| 2 Tab5 second screen | 2a dedicated app screen (ph 3–4) | 2b generic Linux monitor |
| 3 Keyboard bridge | develop HID + event forwarding firmware | validate on the stack |
| 4 External display | — | optional streamed monitor |

Guiding rule, unchanged: nothing on the "Now" column depends on Nexmon or on the
CardputerZero. Everything achievable today runs on the Cardputer + Tab5 already
in hand.
