# Hardware notes — pinout, wiring, per-device

Working notes for the three devices. Anything marked **CONFIRM** is a default
that must be checked against the real hardware before trusting it.

## Cardputer / Cardputer-Adv (ESP32-S3, sensor)

- Board: `m5stack-stamps3` (StampS3), 8 MB flash, **no PSRAM** — keep the sensor
  firmware heap-free / no `String`.
- CSI: native Espressif API. Requires association to a WiFi network so there's
  traffic to measure. Creds via `credentials.ini` or saved Preferences.
- Grove UART out (to hub / Tab5), from `platformio.ini`:
  - `RADAR_TX_PIN = 2`  (GPIO2)
  - `RADAR_RX_PIN = 1`  (GPIO1)
  - `SERIAL_8N1 @ 115200`, bound to `Serial1` (NOT `Serial`, which is USB-CDC).
- The external ILI9341 panel from the original project is **removed** — the
  built-in screen shows sensor status only.

## Tab5 (ESP32-P4, display + touch)

- Panel: 5", 1280×720, capacitive touch. Driver varies by batch (**ST7123** vs
  **ILI9881-class**) — rely on M5Unified/M5GFX auto-detection (`board = m5stack-tab5`),
  do not hard-code a panel driver.
- ESP32-P4 needs the **pioarduino** platform fork (stock `espressif32` lacks P4).
- UART in (from Cardputer / hub), defaults in `platformio.ini` — **CONFIRM** the
  actual Tab5 Grove/header GPIOs:
  - `RADAR_RX_PIN = 18`
  - `RADAR_TX_PIN = 17`
- Has PSRAM → full-screen 16-bit canvas is fine (`gCanvas.setPsram(true)`).

### Keyboard options for the Tab5 (need #3, standalone use)

The Tab5 has no built-in keyboard, but off-the-shelf modules exist — no custom
hardware needed:

- **Tab5 Keyboard** (official, recommended): 70-key physical keyboard made *for*
  the Tab5, plugs into **Ext.Port1**, talks **I2C** + a dedicated interrupt pin
  for low-latency key events. Plug-and-play; use the `m5stack/M5Unit-KEYBOARD`
  library. Also does USB-HID modes (Tab5-as-keyboard to a PC/phone). Best fit for
  a self-contained Tab5 device.
- **Unit CardKB / CardKB2** (mini): I2C over Grove (CardKB addr `0x5F`). CardKB2
  adds UART / BLE-HID / ESP-NOW modes. Smaller, good if the Tab5 Keyboard is too
  big for the enclosure.

Note the transport: these use **I2C** (Ext.Port1 / Grove-I2C), which leaves a
separate **Grove-UART** free for a CSI-sensor link if one is used.

## Compute comparison — Tab5 vs CardputerZero (not the same class)

Physical size ≠ compute: the Tab5 is bigger because of its 5" screen + battery,
not its silicon. The tiny CardputerZero is the more powerful *computer*.

| | Tab5 (ESP32-P4 + C6) | CardputerZero (Pi CM0) |
|---|---|---|
| Class | **microcontroller** (high-end) | **application processor** (SoC) |
| CPU | 2× RISC-V ~400 MHz (+ LP core) | **4× Cortex-A53 ~1 GHz** (≈ Pi Zero 2W) |
| RAM | 32 MB PSRAM | **512 MB** LPDDR2 |
| OS | firmware / FreeRTOS (no OS) | **full Linux** |
| Storage | 16 MB flash | microSD 32 GB |
| Strengths | MIPI display, camera, **real-time I/O**, low power, instant-on | **multitasking, Python, light ML, networking, logging** |

Takeaway: for raw compute / multitasking / RAM / software, the **CardputerZero
wins clearly**; the **Tab5 is a specialised display/real-time MCU**. They're
complementary, not comparable — hence Zero = brain, Tab5 = screen. (And why
neither is an *easy* CSI source: the Zero has power but no CSI API; the Tab5 has
a CSI-capable ESP32-C6 but no OS.)

## CardputerZero (RPi CM0, hub) — not yet in hand

- WiFi chip: Cypress **CYW43459** (no working CSI today — see
  [`../research/nexmon-cyw43459.md`](../research/nexmon-cyw43459.md)).
- **CONFIRM**: native UART reachable without bit-banging the `G4_I2C/UART_SW`
  GPIO mux (brief §8).
- **CONFIRM**: comfortable baud ceiling; measure Cardputer→Zero→Tab5 latency —
  move a hop to local WiFi if UART-chaining is too slow.

## Phase-5 direct wiring (no hub)

Cardputer Grove UART ↔ Tab5 UART, crossed:

```
Cardputer TX (GPIO2) ──────► Tab5 RX (GPIO18)
Cardputer RX (GPIO1) ◄────── Tab5 TX (GPIO17)
GND ───────────────────────── GND        (common ground REQUIRED)
```

Build the Tab5 with `-DRADAR_DIRECT=1` (already the default in its
`platformio.ini`). Common ground between the two devices is mandatory; 3V3 logic
on both sides, so no level shifting needed.
