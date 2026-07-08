# ESP32-C6 CSI notes + Tab5 self-sensing spike

## Espressif CSI quick facts (applies to the Tab5's C6 and any ESP32 node)

- Chip CSI quality ranking: **C5 > C6 > C3 ≈ S3 > original ESP32.** The C6 is a
  strong CSI chip; a pure **ESP32-S3** is the most *proven* reference for a first
  pipeline (Prototype 1) before porting to C6.
- ⚠️ **Known C6 bug — esp-idf #14271:** wrong subcarrier ordering / L-LTF not
  reported correctly in the CSI callback. **Check whether it's fixed in the target
  IDF version before building the pipeline on C6.** If unfixed, prototype on S3.
- Reference pipeline to study: [`espressif/esp-csi`](https://github.com/espressif/esp-csi)
  (documents three topologies: 1 ESP32 + router, 2 ESP32 + router, dedicated TX +
  multiple RX).

## Spike — CSI on the Tab5's ESP32-C6 (self-sensing standalone mode)

**Status: to investigate. Gates need #5 mode 1 (Tab5 as a fully self-contained
radar) ONLY. If it fails, the Tab5 still works standalone via the Cardputer
sensor, and as a 2nd screen with the Zero — so this is upside, not a blocker.**

## The question

**The hardware is already there — this is purely a software question.** The Tab5
ships with an onboard **ESP32-C6-MINI-1U** (Wi-Fi 6 / BT / 802.15.4), soldered to
the board and wired to the main ESP32-P4 over **SDIO** (esp-hosted). It is NOT a
third-party add-on. The P4 itself has no WiFi radio and drives the C6 through
`esp_wifi_remote`. (Confirmed via M5Stack docs + teardown — see Sources.)

So the C6 *is* an ESP32-class WiFi chip that supports the CSI API natively
(`esp_wifi_set_csi_rx_cb`, `esp_wifi_set_csi_config`) when it runs its own app.
The only open question is **software**: are those CSI calls — and, crucially, the
**raw CSI callback buffers** — exposed/forwarded across `esp_wifi_remote` /
esp-hosted to the P4? CSI frames are frequent and bulky, and historically the
hosted (SDIO) interface has not reliably surfaced them. That, not the presence of
a radio, is what gates Tab5 self-sensing.

Sources: M5Stack Tab5 docs (docs.m5stack.com/en/core/Tab5); CNX Software teardown
confirming ESP32-P4 + ESP32-C6-MINI-1U over SDIO.

## Checklist (when doing the spike)

- [ ] Confirm the Tab5's WiFi chip + how it's attached (esp-hosted over SDIO,
      C6 firmware version).
- [ ] Check the `esp_wifi_remote` / `esp-hosted-mcu` version M5Unified/PlatformIO
      pulls in, and whether `esp_wifi_set_csi_rx_cb` is in the remote API surface.
- [ ] Search esp-hosted / esp_wifi_remote issues + docs for "CSI".
- [ ] If the API is present: does the callback actually deliver `wifi_csi_info_t`
      buffers on the P4 side, at a usable rate, without starving SDIO?
- [ ] Measure achievable CSI frame rate over the hosted link vs. the Cardputer's
      native rate (~10–20 Hz target).
- [ ] Decision: (a) works → implement Tab5 self-CSI as PRIMARY mode-1 source,
      reusing the same amplitude/phase-variance estimator as the Cardputer;
      (b) partial/too slow → keep as experimental, default to the Cardputer;
      (c) unavailable → drop mode 1, standalone Tab5 = Cardputer-fed.

## Notes

- If self-CSI works, the Cardputer's estimator (`firmware-cardputer`
  csiCallback) should be factored into a shared sensing module so both the S3
  and the P4/C6 path produce identical `motion_intensity`.
- Alternative if hosted CSI is a dead end but standalone-on-Tab5 is still wanted:
  a tiny custom C6 firmware that does CSI locally and streams `RadarPacket`s to
  the P4 over the internal link — heavier, revisit only if strongly desired.

## Log

| Date | Note |
|------|------|
| 2026-07-08 | Spike defined. Not yet run (needs a bench session on the Tab5). Mode 1 is upside only; modes 2–3 keep the Tab5 useful regardless. |
