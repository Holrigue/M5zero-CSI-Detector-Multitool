# Spike — CSI on the Tab5's ESP32-C6 (self-sensing standalone mode)

**Status: to investigate. Gates need #5 mode 1 (Tab5 as a fully self-contained
radar) ONLY. If it fails, the Tab5 still works standalone via the Cardputer
sensor, and as a 2nd screen with the Zero — so this is upside, not a blocker.**

## The question

Can the Tab5 capture WiFi CSI *by itself*? The ESP32-P4 has **no WiFi radio** —
WiFi is provided by an **ESP32-C6** co-processor over the esp-hosted link
(SDIO). The P4 drives it through `esp_wifi_remote`.

- The ESP32-C6 *is* an ESP32-class WiFi chip and supports the CSI API natively
  (`esp_wifi_set_csi_rx_cb`, `esp_wifi_set_csi_config`) when it runs its own app.
- The open question is whether those CSI calls — and, crucially, the **raw CSI
  callback data** — are exposed/forwarded across `esp_wifi_remote`/esp-hosted to
  the P4. CSI frames are frequent and bulky; historically the hosted interface
  has not reliably surfaced them.

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
