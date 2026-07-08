// =============================================================================
// radar_protocol.h  —  Shared wire contract for the CSI multi-device radar.
//
//   Cardputer (ESP32-S3, sensor)  ──UART──▶  CardputerZero (Linux hub)  ──▶  Tab5
//
// This single header is the canonical definition of the binary RadarPacket that
// travels Cardputer → hub. It is intentionally dependency-free (only <stdint.h>
// / <stddef.h>) so the exact same file can be:
//   * #included by the ESP32 firmware (firmware-cardputer, firmware-tab5), and
//   * compiled by a host tool, or transcribed 1:1 into the Python/Node hub.
//
// Design goals (see docs/protocol-spec.md for the full rationale):
//   * Compact, self-framing, corruption-tolerant: sync byte + length + CRC16.
//   * Little-endian on the wire (native to both ESP32 and the RPi CM0 — no
//     byte-swapping needed on either end). If a big-endian consumer ever shows
//     up, it must swap; the wire order is fixed as LE by this spec.
//   * Fixed field layout, variable trailing blip array. `len` carries the real
//     payload size so a reader never over-reads when blip_count < MAX_BLIPS.
//
// The struct is #pragma pack(1): no compiler padding, so sizeof() equals the
// on-wire size and the layout is identical across toolchains/architectures.
// =============================================================================
#ifndef RADAR_PROTOCOL_H
#define RADAR_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Framing constants ────────────────────────────────────────────────────────
#define RADAR_SYNC       0xAA   // first byte of every frame
#define RADAR_MAX_BLIPS  6      // max contacts carried per packet
#define RADAR_PROTO_VER  1      // bump on any breaking layout change

// calib_state values
enum {
    RADAR_CALIB_IDLE        = 0,  // running, not calibrated / no baseline yet
    RADAR_CALIB_CALIBRATING = 1,  // baseline capture in progress
    RADAR_CALIB_READY       = 2,  // calibrated, actively sensing
};

#pragma pack(push, 1)

// One detected contact ("blip") on the scope.
//
// NOTE on the physics: single-antenna WiFi CSI gives NO reliable bearing and NO
// metric range. `angle_deg` and `distance_est` are therefore RELATIVE/derived
// quantities, not real-world coordinates — see docs/protocol-spec.md §"Semantics".
// The field layout is kept forward-compatible so a future multi-antenna or
// Nexmon-based sensor can populate real angles without changing the wire format.
typedef struct {
    float angle_deg;      // bearing on the scope, degrees. 0 = straight ahead.
    float distance_est;   // relative distance 0..1 (0 = near, 1 = scope edge).
    float confidence;     // 0..1 detection confidence.
} RadarBlip;

// Fixed header (everything before the variable blip array) is 12 bytes:
//   timestamp_ms(4) + calib_state(1) + motion_intensity(4) + blip_count(1)
//   + proto_ver(1) + reserved(1)
typedef struct {
    uint8_t  sync;             // always RADAR_SYNC (0xAA)
    uint8_t  len;              // payload length in bytes: everything between this
                              // field and crc16, i.e. header(12) + blip_count*sizeof(RadarBlip)
    // ---- payload begins here (this is what `len` and the CRC cover) ----
    uint32_t timestamp_ms;     // sensor millis() at capture
    uint8_t  calib_state;      // one of RADAR_CALIB_*
    float    motion_intensity; // 0..1 overall CSI motion energy
    uint8_t  blip_count;       // number of valid entries in blips[] (0..RADAR_MAX_BLIPS)
    uint8_t  proto_ver;        // RADAR_PROTO_VER
    uint8_t  reserved;         // pad / future flags (keep 0)
    RadarBlip blips[RADAR_MAX_BLIPS];
    // ---- payload ends after blip_count blips (NOT necessarily MAX_BLIPS) ----
    uint16_t crc16;            // CRC-16/CCITT-FALSE over the `len` payload bytes
} RadarPacket;

#pragma pack(pop)

// Bytes of payload covered by `len` and the CRC, for a given blip count.
//   = fixed header fields (12) + blip_count * 12
#define RADAR_PAYLOAD_BYTES(blip_count) \
    ((size_t)12 + (size_t)(blip_count) * sizeof(RadarBlip))

// Total on-wire frame size (sync + len + payload + crc16) for a given blip count.
#define RADAR_FRAME_BYTES(blip_count) \
    ((size_t)2 + RADAR_PAYLOAD_BYTES(blip_count) + (size_t)2)

// Largest possible frame (all blips present) — handy for fixed rx buffers.
#define RADAR_FRAME_MAX  RADAR_FRAME_BYTES(RADAR_MAX_BLIPS)

// ── CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect, xorout 0x0000) ──
// Shared by both ends. Small, table-free; runs fine in the sensor loop.
static inline uint16_t radar_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
            else              crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // RADAR_PROTOCOL_H
