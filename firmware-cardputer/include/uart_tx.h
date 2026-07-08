#pragma once
#include <Arduino.h>
#include "radar_protocol.h"
#include "radar_link.h"

// =============================================================================
// RadarUartTx  —  serialise the sensor State into binary RadarPackets and push
// them out a Grove HardwareSerial toward the hub (CardputerZero) or, in a
// Phase-5 bench test, straight to the Tab5.
//
//   * Builds the little-endian, CRC16-framed RadarPacket defined in
//     shared/radar_protocol.h. Because that struct is #pragma pack(1) and the
//     ESP32-S3 is little-endian, we can memcpy the payload region straight out —
//     no field-by-field marshalling.
//   * Also runs a tiny non-blocking line reader for the inbound command channel
//     (link C), handing complete lines to RadarModel::handleCommand().
//   * Heap-free, no String — StampS3 has no PSRAM.
//
// IMPORTANT: this firmware builds with ARDUINO_USB_CDC_ON_BOOT=1, so `Serial` is
// the USB-C port (used for human-readable debug). Always bind this class to a
// HardwareSerial (Serial1/Serial2) on the Grove pins — never `Serial`.
// =============================================================================
class RadarUartTx {
public:
  void begin(HardwareSerial& uart, int rxPin, int txPin, uint32_t baud = 115200) {
    _uart = &uart;
    _uart->begin(baud, SERIAL_8N1, rxPin, txPin);
    _rxLen = 0;
  }

  // Build + transmit one packet from the current model state.
  // `blips` may be null with blipCount 0 (motion-only frame).
  void sendPacket(const RadarModel::State& st, const RadarBlip* blips, uint8_t blipCount) {
    if (!_uart) return;
    if (blipCount > RADAR_MAX_BLIPS) blipCount = RADAR_MAX_BLIPS;

    RadarPacket pkt;                 // stack, ~88 B max
    pkt.sync             = RADAR_SYNC;
    pkt.timestamp_ms     = st.lastUpdateMs;
    pkt.calib_state      = st.calibState;
    pkt.motion_intensity = st.motion;
    pkt.blip_count       = blipCount;
    pkt.proto_ver        = RADAR_PROTO_VER;
    pkt.reserved         = 0;
    for (uint8_t i = 0; i < blipCount; ++i) pkt.blips[i] = blips[i];

    const uint8_t payloadLen = (uint8_t)RADAR_PAYLOAD_BYTES(blipCount);
    pkt.len = payloadLen;

    // CRC covers exactly the payload: the bytes starting at &timestamp_ms.
    // In the packed layout that is (address of pkt) + 2 (past sync+len).
    const uint8_t* payload = reinterpret_cast<const uint8_t*>(&pkt) + 2;
    uint16_t crc = radar_crc16(payload, payloadLen);

    // Emit: sync, len, payload[payloadLen], crc16 (LE). Total = payloadLen + 4.
    _uart->write(pkt.sync);
    _uart->write(pkt.len);
    _uart->write(payload, payloadLen);
    _uart->write((uint8_t)(crc & 0xFF));
    _uart->write((uint8_t)(crc >> 8));
  }

  // Non-blocking: drain inbound bytes, dispatch each complete line (link C
  // commands) to the model. Call every loop().
  void poll(RadarModel& model) {
    if (!_uart) return;
    while (_uart->available()) {
      char c = (char)_uart->read();
      if (c == '\r') continue;
      if (c == '\n') {
        _rxBuf[_rxLen] = '\0';
        if (_rxLen > 0) {
          model.handleCommand(_rxBuf);
          if (model.consumePong()) { _uart->print("PONG\n"); }
        }
        _rxLen = 0;
      } else if (_rxLen < (int)sizeof(_rxBuf) - 1) {
        _rxBuf[_rxLen++] = c;
      } else {
        _rxLen = 0;   // overrun → drop line
      }
    }
  }

private:
  HardwareSerial* _uart = nullptr;
  char _rxBuf[48];
  int  _rxLen = 0;
};
