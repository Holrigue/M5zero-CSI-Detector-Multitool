#pragma once
#include <Arduino.h>
#include <string.h>
#include "radar_protocol.h"

// =============================================================================
// RadarRx  —  Tab5-side receiver for the binary RadarPacket stream.
//
// A small, corruption-tolerant framing state machine: hunts for the 0xAA sync
// byte, validates `len` against the legal blip counts, buffers the payload,
// checks the CRC-16, and decodes the little-endian fields into a RadarPacket.
// Also sends ASCII commands back upstream (link C: touch → hub/sensor).
//
// Heap-free; a single fixed payload buffer sized to RADAR_FRAME_MAX. Non-blocking
// poll(); call it every loop().
//
// In Phase 5 this reads straight from the Cardputer's Grove UART. Later, when the
// hub sits in the middle sending JSON, a sibling JSON reader feeds the same
// RadarPacket shape into the renderer.
// =============================================================================
class RadarRx {
public:
  void begin(HardwareSerial& uart, int rxPin, int txPin, uint32_t baud = 115200) {
    _uart = &uart;
    _uart->begin(baud, SERIAL_8N1, rxPin, txPin);
    _st = WAIT_SYNC;
  }

  // Non-blocking. Returns true (once) when a valid packet was decoded into `out`.
  bool poll(RadarPacket& out) {
    if (!_uart) return false;
    while (_uart->available()) {
      uint8_t c = (uint8_t)_uart->read();
      switch (_st) {
        case WAIT_SYNC:
          if (c == RADAR_SYNC) _st = WAIT_LEN;
          break;
        case WAIT_LEN:
          // len must equal 12 + n*12 for n in 0..MAX_BLIPS.
          if (!validLen(c)) { _stats.badLen++; _st = WAIT_SYNC; break; }
          _len = c; _idx = 0; _st = PAYLOAD;
          break;
        case PAYLOAD:
          _buf[_idx++] = c;
          if (_idx >= _len) _st = CRC_LO;
          break;
        case CRC_LO:
          _crcRx = c; _st = CRC_HI;
          break;
        case CRC_HI:
          _crcRx |= (uint16_t)c << 8;
          _st = WAIT_SYNC;
          if (radar_crc16(_buf, _len) == _crcRx) {
            _stats.good++; _lastRxMs = millis();
            decode(out);
            return true;
          }
          _stats.badCrc++;
          break;
      }
    }
    return false;
  }

  // ---- link C: commands back upstream ----------------------------------------
  void sendCommand(const char* cmd) {
    if (!_uart) return;
    _uart->print(cmd);
    _uart->print('\n');
  }
  void calibrate()           { sendCommand("CAL"); }
  void stopCalibrate()       { sendCommand("CALSTOP"); }
  void setThreshold(float t) { char b[24]; snprintf(b, sizeof(b), "THR %.2f", t); sendCommand(b); }
  void setRate(int hz)       { char b[16]; snprintf(b, sizeof(b), "RATE %d", hz);  sendCommand(b); }
  void ping()                { sendCommand("PING"); }

  bool stale(uint32_t ms = 500) const { return (millis() - _lastRxMs) > ms; }

  struct Stats { uint32_t good = 0, badCrc = 0, badLen = 0; };
  const Stats& stats() const { return _stats; }

private:
  static bool validLen(uint8_t len) {
    if (len < 12) return false;
    uint8_t body = len - 12;
    return (body % sizeof(RadarBlip)) == 0 &&
           (body / sizeof(RadarBlip)) <= RADAR_MAX_BLIPS;
  }

  static uint32_t rdU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  }
  static float rdF32(const uint8_t* p) {
    uint32_t u = rdU32(p);
    float f; memcpy(&f, &u, 4);   // ESP32 is little-endian
    return f;
  }

  // Decode the validated payload buffer into `out`.
  void decode(RadarPacket& out) {
    out.sync             = RADAR_SYNC;
    out.len              = _len;
    out.timestamp_ms     = rdU32(&_buf[0]);
    out.calib_state      = _buf[4];
    out.motion_intensity = rdF32(&_buf[5]);
    out.blip_count       = _buf[9];
    out.proto_ver        = _buf[10];
    out.reserved         = _buf[11];
    if (out.blip_count > RADAR_MAX_BLIPS) out.blip_count = RADAR_MAX_BLIPS;
    for (uint8_t i = 0; i < out.blip_count; ++i) {
      const uint8_t* b = &_buf[12 + i * sizeof(RadarBlip)];
      out.blips[i].angle_deg    = rdF32(&b[0]);
      out.blips[i].distance_est = rdF32(&b[4]);
      out.blips[i].confidence   = rdF32(&b[8]);
    }
    out.crc16 = _crcRx;
  }

  enum St { WAIT_SYNC, WAIT_LEN, PAYLOAD, CRC_LO, CRC_HI };
  HardwareSerial* _uart = nullptr;
  St       _st   = WAIT_SYNC;
  uint8_t  _buf[RADAR_FRAME_MAX];
  uint8_t  _len  = 0;
  uint8_t  _idx  = 0;
  uint16_t _crcRx = 0;
  uint32_t _lastRxMs = 0;
  Stats    _stats;
};
