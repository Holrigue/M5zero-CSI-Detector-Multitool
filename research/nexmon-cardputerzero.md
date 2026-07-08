# Nexmon CSI on the CardputerZero (RPi CM0)

Research journal for the **principal sensing node**. Unlike the classic
Cardputer/Cardputer-Adv (ESP32), the CardputerZero is a **Raspberry Pi CM0**
(BCM2710A1, full Linux), so CSI comes from the **Nexmon** firmware-patch path, not
the Espressif API.

## The plan

Nexmon patches the Broadcom/Cypress WiFi firmware to enable **monitor mode**
(`nexutil -m2`), frame injection, and **native CSI extraction on Linux without
associating** to any network — ideal for passive, device-free sensing.

## The critical unknown — kernel compatibility

This is the gate the whole "CardputerZero as principal node" plan depends on:

- Likely WiFi chip: **BCM43436B0** (same family as the Raspberry Pi Zero 2 W).
  It **is** listed in the Nexmon support table.
- Nexmon is **very sensitive to the exact Linux kernel version**. The relevant
  patch targets roughly **kernel 5.10**; users have reported **failures on 6.12**.
- So a "supported chip" does not guarantee it works on the kernel that ships on
  the M5Stack image.

> **First action on receiving the CardputerZero — before investing any time:**
> 1. Identify the exact WiFi chip: `dmesg | grep -i brcm`, `lsusb`, check
>    `/lib/firmware/brcm/` (or `cypress/`).
> 2. Record the OS image + `uname -r`.
> 3. Cross-check chip + kernel against the Nexmon compatibility table
>    (https://github.com/seemoo-lab/nexmon). If the kernel is far from a supported
>    one, decide: pin/downgrade the kernel, or fall back to an ESP32 sensor node.

## Pointers

- Framework: https://github.com/seemoo-lab/nexmon (and the CSI extension
  seemoo-lab/nexmon_csi).
- If the chip matches, the patch path to study is roughly
  `patches/bcm43436b0/<fw-version>/nexmon/` (e.g. `9_88_4_65`).
- Monitor mode: `nexutil -m2`; CSI collection per the nexmon_csi README.

## Fallback (keeps the project unblocked)

If Nexmon doesn't pan out on the shipped kernel, sensing falls back to an **ESP32
node** (the Tab5's own C6, or a plain ESP32-S3 reference node — see
[`tab5-c6-csi.md`](tab5-c6-csi.md) and `../firmware-cardputer/`). The
CardputerZero then stays the Linux brain (processing, classification, logging)
without being the CSI source. So this journal is high-value but never a hard
blocker.

## Log

| Date | Note |
|------|------|
| 2026-07-08 | Journal created/retargeted. Chip corrected to likely BCM43436B0 (was assumed CYW43459). No hardware yet; kernel-vs-Nexmon check is the first task on receipt. |
