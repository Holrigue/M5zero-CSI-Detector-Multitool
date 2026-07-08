# Scope & safety

This project is **device-free WiFi CSI sensing** for personal security-research
and skills development. These guardrails are part of the design, not an
afterthought — the code is structured to make the safe path the default and the
active path a deliberate, documented choice.

## Authorization scope

- Developed and tested **exclusively on Gab's own hardware and own network.**
- No capture, probing, or traffic generation against third-party networks or
  devices without **explicit, written, per-engagement authorization.**
- Future client use (pentest-style mandates) happens **only on request and with
  documented approval**, defining the target scope, dates, and permitted modes.

## Modes, from always-safe to gated

| Mode | What it does | Status |
|------|--------------|--------|
| **Passive sensing** | Listen to the AP's natural beacons / existing traffic; extract CSI. No frames emitted. | ✅ default, always safe |
| **Traffic generation** | Active probe requests / normal ICMP ping to densify CSI samples. | ⚠️ opt-in, behind an explicit documented switch |
| **Deauthentication / disruption** | — | ⛔ **never implemented, never run**, even on personal gear |

Deauth (and any active jamming/disruption) is a **hard red line**: it moves from
observing a channel to attacking a network — a different legal category. It stays
out of the codebase entirely.

## Code discipline

- **Separation of concerns:** passive-sensing code and any traffic-generation
  code live in clearly separated modules. Active modes must be explicitly enabled
  (compile flag / runtime flag), never on by default, and log that they were used.
- **No deauth path exists** in the code — not gated, simply absent.
- **Capture logging from day one:** every capture records scope/target (own SSID),
  date-time, channel, and duration. This builds the evidence trail habit needed
  for any future authorized client work.

## Why this is defensible research

CSI passive sensing measures perturbations in RF that a receiver is already
entitled to hear (beacons are broadcast). The kit is self-contained (its own
local network) and does not need to touch, join, or alter any site's existing
WiFi. The riskier, dual-use capabilities (active probing) are deliberately
minimized, isolated, and gated; the clearly-offensive ones (deauth) are excluded.
