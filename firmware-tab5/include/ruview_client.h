#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

// =============================================================================
// RuViewClient  —  Tab5-side client of the RuView sensing-server.
//
// Primary path (see docs/prior-art-ruview.md): CSI inference runs on a RuView
// server (the CardputerZero, or a dev box running the RuView Docker on simulated
// data). This class consumes that server and exposes the latest state.
//
// Two transports, one shared frame parser:
//   * WebSocket  ws://<host>:3001/ws/sensing  — live frame stream (primary).
//   * REST       GET http://<host>:3000/api/v1/...  — vitals + a poll fallback.
//
// Endpoints:
//   WS   /ws/sensing              -> live frames (presence, motion, people,
//                                    confidence, signal quality, source, vitals)
//   GET  /api/v1/sensing/latest   -> one frame (REST fallback when WS is down)
//   GET  /api/v1/vital-signs      -> breathing / heart rate (slow; poll ~1 Hz)
//   POST /api/v1/calibration/start|stop
//
// Field names mirror what RuSense's web client reads, so this stays compatible
// with a stock RuView deployment. Parsing is defensive (fields read from the
// document root or a common wrapper; missing fields keep the previous value).
// =============================================================================
struct RuViewState {
  bool     linkOk        = false;   // a frame (WS or REST) arrived recently
  uint32_t lastOkMs      = 0;

  bool     present       = false;
  int      people        = 0;
  float    confidence    = 0.0f;    // 0..1
  float    signalQuality = 0.0f;    // 0..1
  char     motionLevel[16] = "-";   // "absent" / "low" / "active" ...
  char     source[16]      = "-";   // "esp32" / "simulated" ...

  float    breathingBpm  = 0.0f;
  float    breathingConf = 0.0f;
  float    heartBpm      = 0.0f;
  float    heartConf     = 0.0f;
};

class RuViewClient {
public:
  void begin(const char* host, uint16_t httpPort = 3000) {
    _host = host;
    _httpPort = httpPort;
  }

  // ---- WebSocket live stream (primary) ---------------------------------------
  // Bind the state to fill and connect. Call loopWs() every loop().
  void beginWs(RuViewState* st, uint16_t wsPort = 3001, const char* path = "/ws/sensing") {
    _wsState = st;
    s_self = this;
    _ws.begin(_host.c_str(), wsPort, path);
    _ws.onEvent(&RuViewClient::wsTrampoline);
    _ws.setReconnectInterval(2000);
  }
  void loopWs() { _ws.loop(); }
  bool wsConnected() { return _ws.isConnected(); }

  // ---- REST ------------------------------------------------------------------
  // GET /api/v1/sensing/latest — use as a fallback when the WS isn't connected.
  bool pollLatest(RuViewState& st) {
    String body;
    if (!httpGet("/api/v1/sensing/latest", body)) { st.linkOk = false; return false; }
    return applyFrame((const uint8_t*)body.c_str(), body.length(), st);
  }

  // GET /api/v1/vital-signs — breathing / heart rate. Poll ~1 Hz (they're slow).
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
  // Parse one frame (WS text or REST body) into `st`. Shared by both transports.
  bool applyFrame(const uint8_t* data, size_t len, RuViewState& st) {
    JsonDocument doc;
    if (deserializeJson(doc, data, len)) { return false; }

    JsonVariantConst f = doc.as<JsonVariantConst>();
    for (const char* k : {"data", "frame", "latest", "result"}) {
      if (f[k].is<JsonObjectConst>()) { f = f[k]; break; }
    }

    if (f["presence"].is<bool>())        st.present = f["presence"].as<bool>();
    else if (f["occupied"].is<bool>())   st.present = f["occupied"].as<bool>();

    if (f["person_count"].is<int>())     st.people = f["person_count"].as<int>();
    else if (f["people"].is<int>())      st.people = f["people"].as<int>();

    if (f["confidence"].is<float>())           st.confidence    = clamp01(f["confidence"].as<float>());
    if (f["signal_quality_score"].is<float>()) st.signalQuality = clamp01(f["signal_quality_score"].as<float>());

    if (f["motion_level"].is<const char*>())
      copyStr(st.motionLevel, f["motion_level"].as<const char*>(), sizeof(st.motionLevel));
    if (f["source"].is<const char*>())
      copyStr(st.source, f["source"].as<const char*>(), sizeof(st.source));

    readVitals(f["vital_signs"], st);   // vitals may ride the frame too

    st.linkOk = true;
    st.lastOkMs = millis();
    return true;
  }

  void onWs(WStype_t type, uint8_t* payload, size_t len) {
    if (type == WStype_TEXT && _wsState) applyFrame(payload, len, *_wsState);
  }
  static void wsTrampoline(WStype_t type, uint8_t* payload, size_t len) {
    if (s_self) s_self->onWs(type, payload, len);
  }

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
    snprintf(out, n, "http://%s:%u%s", _host.c_str(), (unsigned)_httpPort, path);
  }
  bool httpGet(const char* path, String& body) {
    if (WiFi.status() != WL_CONNECTED) return false;
    char url[128]; makeUrl(url, sizeof(url), path);
    HTTPClient http;
    if (!http.begin(url)) return false;
    http.setConnectTimeout(1500); http.setTimeout(1500);
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
    http.setConnectTimeout(1500); http.setTimeout(1500);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST((uint8_t*)nullptr, 0);
    http.end();
    return code >= 200 && code < 300;
  }

  String          _host = "127.0.0.1";
  uint16_t        _httpPort = 3000;
  WebSocketsClient _ws;
  RuViewState*    _wsState = nullptr;
  static RuViewClient* s_self;
};

// Single-client trampoline target (one RuViewClient per firmware).
inline RuViewClient* RuViewClient::s_self = nullptr;
