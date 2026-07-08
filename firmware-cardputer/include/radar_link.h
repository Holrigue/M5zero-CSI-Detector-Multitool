#pragma once
#include <Arduino.h>
#include "radar_protocol.h"

// =============================================================================
// RadarModel  —  sensor-side state model for the Cardputer (ESP32-S3).
//
// Adapted from the RadarLink class in the original single-device project
// (skizzophrenic/Cardputer-CSI-Human-Detector, MIT). In that project the
// Cardputer was the *display* and RadarLink PARSED an ASCII stream coming FROM a
// separate sensor. Here the Cardputer IS the sensor, so the roles flip:
//
//   * We no longer parse inbound "R,..." frames — the CSI callback in main.cpp
//     produces raw motion locally and feeds it to update().
//   * We keep (and reuse) the reference's hold/coast presence logic, the rolling
//     motion history, and the heap-free / no-String discipline (StampS3 has no
//     PSRAM).
//   * We add a small calibration lifecycle and the inbound command parser for
//     link C (Tab5/hub → sensor): CAL / CALSTOP / THR / RATE / MODE / PING.
//
// The resulting State is what uart_tx.h serialises into a RadarPacket.
// =============================================================================
class RadarModel {
public:
  static constexpr int kHistory = 240;    // motion samples retained for graphing
  static constexpr int kHold     = 150;   // presence coast frames (~10 s @ 15 Hz)

  struct State {
    uint32_t seq          = 0;                    // frames emitted
    bool     presence     = false;                // someone detected (post hold/coast)
    float    motion       = 0.0f;                 // 0..1 after hold/coast/decay
    int      rssi         = 0;                     // dBm of the sensed link
    uint8_t  calibState   = RADAR_CALIB_IDLE;      // RADAR_CALIB_*
    float    threshold    = 0.15f;                 // motion presence threshold
    int      rateHz       = 15;                    // requested packet rate
    uint32_t lastUpdateMs = 0;                     // millis() of last update()
  };

  // Feed ONE raw motion sample (0..1) plus current RSSI from the CSI layer.
  // Applies the reference hold/coast logic and advances the frame counter.
  // Call this at the packet rate (see rateHz), once per outgoing packet.
  void update(float rawMotion, int rssi) {
    uint32_t now = millis();
    _state.rssi         = rssi;
    _state.lastUpdateMs = now;

    // Auto-finish calibration once the baseline window elapses.
    if (_state.calibState == RADAR_CALIB_CALIBRATING && now >= _calibUntilMs)
      _state.calibState = RADAR_CALIB_READY;

    float m = rawMotion;
    if (m < 0.0f) m = 0.0f;
    if (m > 1.0f) m = 1.0f;

    bool present;
    if (_state.calibState == RADAR_CALIB_CALIBRATING) {
      // During baseline capture we report motion for feedback but no presence.
      present = false;
    } else if (m > _state.threshold) {
      _holdCnt    = kHold;
      _heldMotion = m;
      present     = true;
    } else if (_holdCnt > 0) {
      _holdCnt--;
      float fade = (float)_holdCnt / (float)kHold;
      m       = _heldMotion * (0.10f + 0.90f * fade);   // graceful decay to 10%
      present = true;
    } else {
      present = false;
      m       = 0.0f;
    }

    _state.presence = present;
    _state.motion   = m;
    _state.seq++;

    _hist[_head] = m;
    _head = (_head + 1) % kHistory;
  }

  // ---- calibration lifecycle -------------------------------------------------
  // Arms a baseline-capture window. main.cpp should poll consumeResetRequest()
  // and, when true, reset the CSI normalisation buffers so the baseline retrains.
  void beginCalibrate(uint32_t ms = 3000) {
    _state.calibState = RADAR_CALIB_CALIBRATING;
    _calibUntilMs     = millis() + ms;
    _holdCnt          = 0;
    _heldMotion       = 0.0f;
    _resetPending     = true;
  }
  void stopCalibrate() {
    _state.calibState = RADAR_CALIB_READY;
  }
  // One-shot: true exactly once after a calibrate request, so main.cpp can reset
  // the CSI amplitude/phase buffers without this class touching WiFi internals.
  bool consumeResetRequest() {
    bool r = _resetPending; _resetPending = false; return r;
  }

  // ---- inbound command channel (link C: Tab5/hub → sensor) -------------------
  // Parses one newline-stripped ASCII command in place. Returns true if handled.
  // Recognises: CAL, CALSTOP, THR <f>, RATE <hz>, MODE <name>, PING.
  bool handleCommand(const char* line) {
    if (!line || !line[0]) return false;
    if (!strncmp(line, "CALSTOP", 7)) { stopCalibrate();  return true; }
    if (!strncmp(line, "CAL", 3))     { beginCalibrate();  return true; }
    if (!strncmp(line, "THR", 3)) {
      float t = atof(line + 3);
      if (t > 0.0f && t < 1.0f) _state.threshold = t;
      return true;
    }
    if (!strncmp(line, "RATE", 4)) {
      int hz = atoi(line + 4);
      if (hz >= 1 && hz <= 50) _state.rateHz = hz;
      return true;
    }
    if (!strncmp(line, "MODE", 4)) { return true; }   // reserved: view switch
    if (!strncmp(line, "PING", 4)) { _pong = true; return true; }
    return false;
  }
  bool consumePong() { bool p = _pong; _pong = false; return p; }

  // ---- accessors -------------------------------------------------------------
  const State& state() const { return _state; }
  float historyAt(int idx) const { return _hist[(_head + idx) % kHistory]; }
  static constexpr int historySize() { return kHistory; }

private:
  State    _state;
  float    _hist[kHistory] = {};   // zero-init: safe before first update()
  int      _head        = 0;
  int      _holdCnt     = 0;
  float    _heldMotion  = 0.0f;
  uint32_t _calibUntilMs = 0;
  bool     _resetPending = false;
  bool     _pong         = false;
};
