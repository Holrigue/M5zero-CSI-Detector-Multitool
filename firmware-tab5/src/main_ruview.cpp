// =============================================================================
// radar-multi · firmware-tab5 · RuView dashboard client (PRIMARY app)
//
// The Tab5 (ESP32-P4 + C6) as a native dashboard for a RuView sensing-server:
// it joins the kit's WiFi, polls the RuView REST API, and renders presence /
// motion / people / confidence / vital signs on the 5" screen, with a touch
// CALIBRATE control. See docs/prior-art-ruview.md.
//
// Dev with NO hardware: run the RuView Docker image on a dev box
//   docker run -p 3000:3000 ruvnet/wifi-densepose
// point RUVIEW_HOST at that box, and this dashboard renders its simulated data.
//
// The older on-Tab5 renderer (main_direct.cpp) remains the offline no-server
// fallback (direct ESP32→Tab5 UART); pick it with the `tab5-direct` PlatformIO env.
// =============================================================================
#include <M5Unified.h>
#include <WiFi.h>
#include "ruview_client.h"

#ifndef KIT_SSID
#define KIT_SSID ""            // the kit's own 2.4 GHz network (set in credentials.ini)
#endif
#ifndef KIT_PASS
#define KIT_PASS ""
#endif
#ifndef RUVIEW_HOST
#define RUVIEW_HOST "127.0.0.1"  // RuView server IP (CardputerZero, or dev Docker box)
#endif
#ifndef RUVIEW_PORT
#define RUVIEW_PORT 3000            // HTTP REST
#endif
#ifndef RUVIEW_WS_PORT
#define RUVIEW_WS_PORT 3001         // WebSocket /ws/sensing (Docker maps 3000->3001)
#endif

static RuViewClient gClient;
static RuViewState  gSt;
static M5Canvas     gCanvas(&M5.Display);
static int          W, H;

static uint32_t gPollOk = 0, gPollErr = 0;
static float    gConfHist[256];   // confidence sparkline ring
static int      gConfHead = 0, gConfLen = 0;

struct Btn { int x, y, w, h; const char* label; };
static Btn gCalBtn;

// ── helpers ──────────────────────────────────────────────────────────────────
static uint16_t C(uint8_t r, uint8_t g, uint8_t b) { return gCanvas.color565(r, g, b); }

static void pushConf(float v) {
  gConfHist[gConfHead] = v;
  gConfHead = (gConfHead + 1) % 256;
  if (gConfLen < 256) gConfLen++;
}

static void wifiConnect() {
  if (!KIT_SSID[0]) return;
  WiFi.mode(WIFI_STA);
  if (KIT_PASS[0]) WiFi.begin(KIT_SSID, KIT_PASS);
  else             WiFi.begin(KIT_SSID);
}

// ── rendering ────────────────────────────────────────────────────────────────
static void tile(int x, int y, int w, int h, const char* label, const char* value,
                 uint16_t accent) {
  gCanvas.drawRoundRect(x, y, w, h, 10, C(40, 46, 54));
  gCanvas.setTextColor(C(140, 150, 160));
  gCanvas.setTextSize(2);
  gCanvas.drawString(label, x + 16, y + 14);
  gCanvas.setTextColor(accent);
  gCanvas.setTextSize(4);
  gCanvas.drawString(value, x + 16, y + h - 52);
}

static void bar(int x, int y, int w, int h, float v, uint16_t col) {
  gCanvas.drawRoundRect(x, y, w, h, 4, C(40, 46, 54));
  int fill = (int)((w - 4) * (v < 0 ? 0 : v > 1 ? 1 : v));
  gCanvas.fillRoundRect(x + 2, y + 2, fill, h - 4, 3, col);
}

static void render() {
  gCanvas.fillSprite(C(10, 12, 16));
  bool live = !RuViewClient::stale(gSt, 3000);
  char buf[48];

  // ── top banner: presence ──────────────────────────────────────────────────
  bool present = live && gSt.present;
  gCanvas.fillRect(0, 0, W, 64, present ? C(0, 90, 45) : C(35, 12, 12));
  gCanvas.setTextSize(4);
  gCanvas.setTextColor(present ? C(120, 255, 170) : C(255, 120, 120));
  gCanvas.drawString(present ? "PRESENCE DETECTED" : "AREA CLEAR", 20, 14);

  // link + source pill (top-right)
  const char* linkTxt = live ? "LINK OK" : "NO SERVER";
  gCanvas.setTextSize(2);
  gCanvas.setTextColor(live ? C(120, 255, 170) : C(255, 120, 120));
  gCanvas.drawString(linkTxt, W - gCanvas.textWidth(linkTxt) - 20, 8);
  snprintf(buf, sizeof(buf), "src: %s", gSt.source);
  gCanvas.setTextColor(C(150, 160, 170));
  gCanvas.drawString(buf, W - gCanvas.textWidth(buf) - 20, 36);

  // ── stat tiles row ──────────────────────────────────────────────────────────
  int pad = 20, top = 84;
  int tw = (W - pad * 5) / 4, th = 130;
  char v[16];
  snprintf(v, sizeof(v), "%d", gSt.people);
  tile(pad, top, tw, th, "PEOPLE", v, C(120, 200, 255));
  snprintf(v, sizeof(v), "%d%%", (int)(gSt.confidence * 100));
  tile(pad * 2 + tw, top, tw, th, "CONFIDENCE", v, C(120, 255, 170));
  snprintf(v, sizeof(v), "%d%%", (int)(gSt.signalQuality * 100));
  tile(pad * 3 + tw * 2, top, tw, th, "SIGNAL", v, C(255, 210, 120));
  tile(pad * 4 + tw * 3, top, tw, th, "MOTION", gSt.motionLevel, C(200, 170, 255));

  // ── vitals card ──────────────────────────────────────────────────────────────
  int vy = top + th + 20, vh = 150;
  gCanvas.drawRoundRect(pad, vy, W - pad * 2, vh, 12, C(40, 46, 54));
  gCanvas.setTextSize(2); gCanvas.setTextColor(C(140, 150, 160));
  gCanvas.drawString("VITAL SIGNS  (still subject only)", pad + 16, vy + 12);

  gCanvas.setTextColor(C(255, 140, 160));
  snprintf(buf, sizeof(buf), "Heart  %.0f bpm", gSt.heartBpm);
  gCanvas.setTextSize(3); gCanvas.drawString(buf, pad + 24, vy + 48);
  bar(pad + 24, vy + 92, (W - pad * 2) / 2 - 48, 16, gSt.heartConf, C(255, 90, 120));

  int cx = pad + (W - pad * 2) / 2 + 8;
  gCanvas.setTextColor(C(140, 200, 255));
  snprintf(buf, sizeof(buf), "Breath %.0f bpm", gSt.breathingBpm);
  gCanvas.setTextSize(3); gCanvas.drawString(buf, cx, vy + 48);
  bar(cx, vy + 92, (W - pad * 2) / 2 - 48, 16, gSt.breathingConf, C(90, 160, 255));

  // ── confidence sparkline ──────────────────────────────────────────────────────
  int gy = vy + vh + 20, gh = H - (vy + vh + 20) - 110;
  if (gh > 20) {
    gCanvas.drawRoundRect(pad, gy, W - pad * 2, gh, 10, C(40, 46, 54));
    int plotW = W - pad * 2 - 8;
    for (int i = 0; i < gConfLen && i < plotW; i++) {
      int idx = (gConfHead - gConfLen + i + 512) % 256;
      float val = gConfHist[idx];
      int hh = (int)((gh - 8) * val);
      gCanvas.drawFastVLine(pad + 4 + i, gy + gh - 4 - hh, hh, C(0, 150, 90));
    }
    gCanvas.setTextSize(1); gCanvas.setTextColor(C(120, 130, 140));
    gCanvas.drawString("confidence history", pad + 10, gy + 8);
  }

  // ── CALIBRATE button ──────────────────────────────────────────────────────────
  gCanvas.drawRoundRect(gCalBtn.x, gCalBtn.y, gCalBtn.w, gCalBtn.h, 12, C(120, 200, 255));
  gCanvas.setTextSize(3); gCanvas.setTextColor(C(120, 200, 255));
  gCanvas.drawString(gCalBtn.label, gCalBtn.x + (gCalBtn.w - gCanvas.textWidth(gCalBtn.label)) / 2,
                     gCalBtn.y + gCalBtn.h / 2 - 12);

  // ── footer ──────────────────────────────────────────────────────────────────
  gCanvas.setTextSize(1); gCanvas.setTextColor(C(110, 120, 130));
  snprintf(buf, sizeof(buf), "server %s:%d  ws:%s  wifi %s (%d dBm)  rest ok:%lu err:%lu",
           RUVIEW_HOST, RUVIEW_PORT, gClient.wsConnected() ? "up" : "down",
           WiFi.status() == WL_CONNECTED ? WiFi.SSID().c_str() : "-",
           (int)WiFi.RSSI(), (unsigned long)gPollOk, (unsigned long)gPollErr);
  gCanvas.drawString(buf, pad, H - 16);

  gCanvas.pushSprite(0, 0);
}

// ── touch ─────────────────────────────────────────────────────────────────────
static bool inBtn(int x, int y, const Btn& b) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

// =============================================================================
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);
  W = M5.Display.width();
  H = M5.Display.height();
  gCanvas.setPsram(true);
  gCanvas.createSprite(W, H);

  gCalBtn = { W - 300, H - 100, 260, 70, "CALIBRATE" };

  Serial.begin(115200);
  wifiConnect();
  gClient.begin(RUVIEW_HOST, RUVIEW_PORT);
  gClient.beginWs(&gSt, RUVIEW_WS_PORT);   // live frame stream (primary)
}

void loop() {
  M5.update();

  // touch → calibrate
  if (M5.Touch.isEnabled()) {
    auto t = M5.Touch.getDetail();
    if (t.wasPressed() && inBtn(t.x, t.y, gCalBtn)) gClient.calibrateStart();
  }

  // retry WiFi if dropped
  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t lastTry = 0;
    if (millis() - lastTry > 5000) { lastTry = millis(); wifiConnect(); }
  }

  // live frame stream (primary)
  gClient.loopWs();

  // REST fallback: only poll /sensing/latest when the WebSocket isn't connected
  if (!gClient.wsConnected()) {
    static uint32_t lastLatest = 0;
    if (millis() - lastLatest >= 1000) {
      lastLatest = millis();
      if (gClient.pollLatest(gSt)) gPollOk++; else gPollErr++;
    }
  }

  // poll vitals ~1 Hz (dedicated endpoint; they update slowly)
  static uint32_t lastVitals = 0;
  if (millis() - lastVitals >= 1000) {
    lastVitals = millis();
    gClient.pollVitals(gSt);
  }

  // sample confidence into the sparkline at a steady cadence (source-agnostic)
  static uint32_t lastConf = 0;
  if (millis() - lastConf >= 100) {
    lastConf = millis();
    pushConf(RuViewClient::stale(gSt, 3000) ? 0.0f : gSt.confidence);
  }

  // render ~20 fps
  static uint32_t lastDraw = 0;
  if (millis() - lastDraw >= 50) { lastDraw = millis(); render(); }
}
