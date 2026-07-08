#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// =============================================================================
// RuViewClient  —  Tab5-side client of the RuView sensing-server REST API.
//
// The primary architecture (see docs/prior-art-ruview.md) puts the CSI inference
// on a RuView server (on the CardputerZero, or a dev box running the RuView
// Docker image on simulated data). This class polls that server over HTTP and
// exposes the latest state for the dashboard to render.
//
// Endpoints used (RuView native API, default HTTP port 3000):
//   GET  /api/v1/sensing/latest   -> latest frame (presence, motion, people,
//                                    confidence, signal quality, source, vitals)
//   GET  /api/v1/vital-signs      -> breathing / heart rate (updates slowly)
//   POST /api/v1/calibration/start|stop
//
// REST polling (not the /ws/sensing WebSocket) is used deliberately for a robust
// first version: simple, reconnect-free, and vitals update slowly anyway. A WS
// path can be added later for smoother live motion.
//
// Field names mirror what RuSense's web client reads from the same server, so
// this stays compatible with a stock RuView deployment. Parsing is defensive:
// fields are read from the document root or a common wrapper, and missing fields
// leave the previous value untouched.
// =============================================================================
struct RuViewState {
  bool     linkOk        = false;   // last poll round-trip succeeded
  uint32_t lastOkMs      = 0;

  bool     present       = false;
  int      people        = 0;
  float    confidence    = 0.0f;    // 0..1 detection confidence
  float    signalQuality = 0.0f;    // 0..1
  char     motionLevel[16] = "—";   // e.g. "absent" / "low" / "active"
  char     source[16]      = "—";   // "esp32" / "simulated" / ...

  float    breathingBpm  = 0.0f;
  float    breathingConf = 0.0f;
  float    heartBpm      = 0.0f;
  float    heartConf     = 0.0f;
};

class RuViewClient {
public:
  void begin(const char* host, uint16_t port = 3000) {
    _host = host;
    _port = port;
  }

  // GET /api/v1/sensing/latest → update presence/motion/people/confidence/etc.
  bool pollLatest(RuViewState& st) {
    String body;
    if (!httpGet("/api/v1/sensing/latest", body)) { st.linkOk = false; return false; }

    JsonDocument doc;   // ArduinoJson v7 elastic document
    if (deserializeJson(doc, body)) { st.linkOk = false; return false; }

    // Accept either a bare frame or a wrapper ({data|frame|latest|result:{...}}).
    JsonVariantConst f = doc.as<JsonVariantConst>();
    for (const char* k : {"data", "frame", "latest", "result"}) {
      if (f[k].is<JsonObjectConst>()) { f = f[k]; break; }
    }

    if (f["presence"].is<bool>())        st.present = f["presence"].as<bool>();
    else if (f["occupied"].is<bool>())   st.present = f["occupied"].as<bool>();

    if (f["person_count"].is<int>())     st.people = f["person_count"].as<int>();
    else if (f["people"].is<int>())      st.people = f["people"].as<int>();

    if (f["confidence"].is<float>())            st.confidence    = clamp01(f["confidence"].as<float>());
    if (f["signal_quality_score"].is<float>())  st.signalQuality = clamp01(f["signal_quality_score"].as<float>());

    if (f["motion_level"].is<const char*>())
      copyStr(st.motionLevel, f["motion_level"].as<const char*>(), sizeof(st.motionLevel));
    if (f["source"].is<const char*>())
      copyStr(st.source, f["source"].as<const char*>(), sizeof(st.source));

    // Vitals may ride the frame too; the dedicated endpoint is the reliable source.
    readVitals(f["vital_signs"], st);

    st.linkOk = true;
    st.lastOkMs = millis();
    return true;
  }

  // GET /api/v1/vital-signs → breathing / heart rate. Call at ~1 Hz (they're slow).
  bool pollVitals(RuViewState& st) {
    String body;
    if (!httpGet("/api/v1/vital-signs", body)) return false;
    JsonDocument doc;
    if (deserializeJson(doc, body)) return false;
    JsonVariantConst vs = doc["vital_signs"].is<JsonObjectConst>()
                          ? doc["vital_signs"] : doc.as<JsonVariantConst>();
    readVitals(vs, st);
    return true;
  }

  bool calibrateStart() { return httpPost("/api/v1/calibration/start"); }
  bool calibrateStop()  { return httpPost("/api/v1/calibration/stop");  }

  static bool stale(const RuViewState& st, uint32_t ms = 3000) {
    return !st.linkOk || (millis() - st.lastOkMs) > ms;
  }

private:
  static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
  static void copyStr(char* dst, const char* src, size_t n) {
    if (!src) return; strncpy(dst, src, n - 1); dst[n - 1] = '\0';
  }
  static void readVitals(JsonVariantConst vs, RuViewState& st) {
    if (!vs.is<JsonObjectConst>()) return;
    if (vs["breathing_rate_bpm"].is<float>())   st.breathingBpm  = vs["breathing_rate_bpm"].as<float>();
    if (vs["breathing_confidence"].is<float>()) st.breathingConf = clamp01(vs["breathing_confidence"].as<float>());
    if (vs["heart_rate_bpm"].is<float>())       st.heartBpm      = vs["heart_rate_bpm"].as<float>();
    if (vs["heartbeat_confidence"].is<float>()) st.heartConf     = clamp01(vs["heartbeat_confidence"].as<float>());
  }

  void makeUrl(char* out, size_t n, const char* path) {
    snprintf(out, n, "http://%s:%u%s", _host.c_str(), (unsigned)_port, path);
  }

  bool httpGet(const char* path, String& body) {
    if (WiFi.status() != WL_CONNECTED) return false;
    char url[128]; makeUrl(url, sizeof(url), path);
    HTTPClient http;
    if (!http.begin(url)) return false;
    http.setConnectTimeout(1500);
    http.setTimeout(1500);
    int code = http.GET();
    bool ok = (code == 200);
    if (ok) body = http.getString();
    http.end();
    return ok;
  }

  bool httpPost(const char* path) {
    if (WiFi.status() != WL_CONNECTED) return false;
    char url[128]; makeUrl(url, sizeof(url), path);
    HTTPClient http;
    if (!http.begin(url)) return false;
    http.setConnectTimeout(1500);
    http.setTimeout(1500);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST((uint8_t*)nullptr, 0);
    http.end();
    return code >= 200 && code < 300;
  }

  String   _host = "127.0.0.1";
  uint16_t _port = 3000;
};
