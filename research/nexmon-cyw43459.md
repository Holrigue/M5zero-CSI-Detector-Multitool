# Track B research journal — Nexmon CSI on the CardputerZero (CYW43459)

**Status: exploratory, blocked on hardware. Never a prerequisite for Track A.**

This journal tracks whether CSI can be extracted *on the CardputerZero itself*.
If it works someday, the CM0 could replace the ESP32 as the sensor. If it never
works, nothing is lost — the ESP32 remains the sensor. Keep that framing.

## The problem in one paragraph

The Cardputer/Cardputer-Adv extract CSI via Espressif's proprietary API
(`esp_wifi_set_csi_rx_cb`), which exists **only** on ESP32 WiFi silicon. The
CardputerZero uses a Cypress **CYW43459** (Broadcom/Cypress family). Extracting
CSI there means patching the WiFi firmware, which is what
[Nexmon CSI](https://github.com/seemoo-lab/nexmon_csi) does — but:

- The CardputerZero was just announced (Kickstarter, delivery ~Nov 2026); no
  Nexmon patch is known to exist for the CYW43459 / RP3A0 revision.
- An existing patch for a *different* chip (e.g. bcm43455c0 on the Pi 3B+/Zero 2 W)
  is **not** guaranteed to apply to a different silicon revision — may need real
  reverse engineering.
- Nexmon patches are sensitive to the **exact Linux kernel version**.

## Open checklist (do once the CardputerZero is in hand)

- [ ] Identify the exact WiFi chip + firmware revision on the shipped unit
      (`dmesg | grep -i brcm`, `/lib/firmware/cypress/` or `brcm/`, chip probe).
- [ ] Record the OS image + `uname -r` kernel version.
- [ ] Search `seemoo-lab/nexmon_csi` issues/branches for CYW43459 / RP3A0 / CM0.
- [ ] Check the broader Nexmon (`seemoo-lab/nexmon`) chip support matrix.
- [ ] Assess how close bcm43455c0 (closest documented Cypress part) is to 43459.
- [ ] Decide: (a) usable patch exists → try extraction; (b) feasible with effort
      → time-box a reverse-engineering spike; (c) infeasible → stay on ESP32.

## Watch list

- `seemoo-lab/nexmon_csi` issues — any mention of CYW43459 / RP3A0 / CM0 / Zero 2.
- `seemoo-lab/nexmon` chip support table updates.
- M5Stack CardputerZero docs/forums for the shipped OS image + kernel.

## Log

| Date | Note |
|------|------|
| 2026-07-08 | Journal created. No hardware yet. No known CYW43459 Nexmon CSI patch as of this date. Track A (ESP32 sensing) is the foundation; this stays a background watch. |
