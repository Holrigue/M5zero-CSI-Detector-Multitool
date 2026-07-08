# Prior art & adoption — RuView / RuSense (Ragnar)

We evaluated the most mature open project in this exact space and decided to
**build on it** rather than reinvent the CSI pipeline.

- **[`ruvnet/ruview`](https://github.com/ruvnet/ruview)** ("RuView", MIT, © 2024 rUv)
  — a WiFi-CSI sensing platform: ESP32 CSI node firmware + a Rust inference server
  + pretrained models. This is the engine.
- **[`PierreGode/Ragnar`](https://github.com/PierreGode/Ragnar)** (MIT) — a
  Raspberry-Pi security appliance whose **RuSense** feature *integrates* RuView
  (vendors the prebuilt server + a browser flasher). Good reference for how to
  wrap RuView into a product (web UI, alerts, geofence, monitoring modes).

Both are MIT — reuse is fine **with attribution** (added to our `LICENSE`).

## What RuView already does (so we don't rebuild it)

- **CSI node firmware** for **ESP32-S3** (and a C6 variant) — `firmware/esp32-csi-node/`
  — streams CSI over **UDP** to the server. Provisioned with `provision.py`
  (`--ssid --password --target-ip --target-port --node-id --channel …`), writing
  the `csi_cfg` NVS at `0x9000`. Browser-flashable (Web Serial / esptool-js).
- **Sensing server** (Rust crate `wifi-densepose`, Docker `ruvnet/wifi-densepose`,
  **arm64** available) — ingests CSI (UDP :5005), runs inference, exposes a large
  **REST `/api/v1/*` + WebSocket** API on `:3000` (calibration, adaptive
  train/models, events, embeddings, mesh, …).
- **Inference**: presence, motion, people-count, 17-keypoint pose, breathing/heart
  rate, fall detection — with small pretrained models on Hugging Face
  (`ruvnet/wifi-densepose-pretrained`) that run on a Pi. Phase-variance presence
  fallback needs **no model**.
- **Dev with no hardware**: `docker run -p 3000:3000 ruvnet/wifi-densepose` serves
  the full API on **simulated data** — we can build/validate the Tab5 client today.

## Decision — how we adopt it (single-node first)

**Primary architecture (build on RuView), single ESP32-S3 node:**

```
1× ESP32-S3 CSI node (RuView firmware)
        │ CSI over UDP (:5005), on the kit's own 2.4 GHz WiFi
        ▼
CardputerZero (Linux)  →  runs the RuView sensing-server (arm64 Docker / binary)
        │ REST /api/v1/* + WebSocket (:3000), over the kit's local WiFi
        ▼
Tab5 (ESP32-P4 + C6)   →  NATIVE dashboard client of the RuView API
        presence / motion / vitals / heatmap on the 5" screen + touch
```

Adopt:
- RuView's **ESP32-S3 node firmware + provisioning** as our CSI sensor (replaces
  our custom sensor firmware for the primary path).
- RuView's **sensing-server on the CardputerZero** as the brain/inference.
- A **Tab5 client** we write against RuView's REST/WS API (this is the main new
  firmware work — our Tab5 renderer feeds from the API instead of our RadarPacket).
- Product ideas from **RuSense**: browser flashing UX, calibration wizard,
  Record→Train→Active loop, confidence gating, monitoring modes (security/health),
  sighting log. Emulate as useful; not all at once.

Defer / keep optional:
- **Multi-node ESP32 mesh** (multistatic fusion → accurate people-count,
  positioning, pose) → **Phase 2, optional.** Start with **one node** (presence,
  motion, vitals work single-node). ⚠️ If we ever add nodes, **don't mix S3 and
  C6** (different CSI widths break fusion) — pick one chip for the whole mesh.
- **Nexmon on the CardputerZero** → **no longer needed for the primary path.**
  RuView uses an ESP32 for CSI and the Pi as server, so the CardputerZero is the
  *server*, not the CSI source. Nexmon becomes a purely optional experiment
  (using the Pi's own radio as an extra illuminator) — see
  [`nexmon-cardputerzero.md`](nexmon-cardputerzero.md).

## Honest caveats

- **CM0 horsepower:** RuView's light path (presence/motion/vitals, 8 KB model)
  runs on a Pi; heavy pose / world-model are benchmarked on a Pi 5 / RTX. The
  CardputerZero (Pi Zero 2W-class, 512 MB) should handle the light path — **verify
  on receipt**; fall back to presence/vitals only if pose is too heavy.
- **Our existing code** (`shared/radar_protocol.h`, `firmware-cardputer`,
  `firmware-tab5` binary rx, role state machine) becomes the **offline/no-server
  fallback path** (direct ESP32→Tab5 UART, no Pi) — kept as a hardware-minimal
  mode and learning artifact, not the primary route.
- **Privacy/scope** unchanged and reinforced — RuView/RuSense frame it the same
  way ("monitor spaces you own/are authorized to"). See
  [`../docs/scope-and-safety.md`](../docs/scope-and-safety.md).

## Immediate, no-hardware next step

Run `docker run -p 3000:3000 ruvnet/wifi-densepose` (simulated data) and build the
**Tab5 RuView-client** against the live REST/WS API — the whole dashboard can be
developed and validated before any node or CardputerZero is in hand.
