#pragma once
#include <Arduino.h>
#include <string.h>

// =============================================================================
// RoleManager  —  the Tab5's role state machine (requirements.md §5).
//
// The Tab5 adapts its role at runtime to whatever hardware is present, by this
// fixed priority:
//
//   1. CardputerZero (hub) seen on the link   -> SECONDARY_DISPLAY
//        the Zero (Linux) is the brain and pushes the full-size scanning map;
//        the Tab5 just renders and sends touch/commands back.
//   2. else Tab5 self-CSI available (C6 spike) -> PRIMARY_SELF
//        standalone radar sensing on the Tab5's own ESP32-C6.
//   3. else Cardputer packets arriving         -> PRIMARY_REMOTE
//        standalone radar, sensing fed by the Cardputer over the link.
//   4. else                                     -> NO_SOURCE
//
// Detection is passive: the hub emits a periodic "RM-HUB ..." heartbeat on the
// link (see protocol-spec.md §Link B); a fresh heartbeat means the hub is
// present, a timeout means it went away and we fall back automatically. Binary
// RadarPacket arrivals are reported via notePacket(). No user action — plug in
// the Zero and it becomes the brain; unplug it and the Tab5 goes standalone.
// =============================================================================
enum RadarRole {
  ROLE_NO_SOURCE = 0,
  ROLE_PRIMARY_SELF,       // standalone, Tab5's own C6 CSI (gated on the spike)
  ROLE_PRIMARY_REMOTE,     // standalone, fed by the Cardputer over the link
  ROLE_SECONDARY_DISPLAY,  // hub is the brain; Tab5 renders the full-size map
};

class RoleManager {
public:
  static constexpr uint32_t kHubTimeoutMs = 3000;  // hub considered gone after this
  static constexpr uint32_t kPktTimeoutMs = 1500;  // sensor stream considered gone

  // Set true once the C6 self-CSI spike proves raw CSI reaches the P4 (need #5,
  // mode 1). Until then this stays false and standalone falls back to the
  // Cardputer-fed path — the Tab5 is still a working radar.
  void setSelfCsiAvailable(bool ok) { _selfCsi = ok; }

  // Feed one ASCII byte — the bytes the binary RadarPacket parser rejected.
  // Assembles lines and refreshes the hub heartbeat on an "RM-HUB" announce.
  // Returns the completed line (for later routing of hub map data), else null.
  const char* feedAscii(uint8_t c) {
    if (c == '\n' || c == '\r') {
      if (_len == 0) return nullptr;
      _line[_len] = '\0';
      const char* done = _line;
      if (strncmp(_line, "RM-HUB", 6) == 0) { _lastHubMs = millis(); _everHub = true; }
      _len = 0;
      return done;
    }
    if (_len < (int)sizeof(_line) - 1) _line[_len++] = (char)c;
    else _len = 0;                         // overrun -> drop line
    return nullptr;
  }

  // Call whenever a binary RadarPacket was decoded from the link.
  void notePacket() { _lastPktMs = millis(); _everPkt = true; }

  bool hubPresent() const {
    return _everHub && (millis() - _lastHubMs) < kHubTimeoutMs;
  }
  bool sensorFresh() const {
    return _everPkt && (millis() - _lastPktMs) < kPktTimeoutMs;
  }

  RadarRole current() const {
    if (hubPresent())  return ROLE_SECONDARY_DISPLAY;
    if (_selfCsi)      return ROLE_PRIMARY_SELF;
    if (sensorFresh()) return ROLE_PRIMARY_REMOTE;
    return ROLE_NO_SOURCE;
  }

  static const char* name(RadarRole r) {
    switch (r) {
      case ROLE_SECONDARY_DISPLAY: return "SECONDARY / hub";
      case ROLE_PRIMARY_SELF:      return "PRIMARY (self CSI)";
      case ROLE_PRIMARY_REMOTE:    return "PRIMARY (remote)";
      default:                     return "NO SOURCE";
    }
  }

private:
  bool     _selfCsi = false;
  bool     _everHub = false;
  bool     _everPkt = false;
  uint32_t _lastHubMs = 0;
  uint32_t _lastPktMs = 0;
  char     _line[96];
  int      _len = 0;
};
