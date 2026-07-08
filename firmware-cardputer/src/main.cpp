// =============================================================================
// radar-multi · firmware-cardputer  —  CSI sensor node (M5Stack Cardputer-Adv,
// ESP32-S3 / StampS3).
//
// Role in the multi-device system: SENSE WiFi CSI locally and stream compact
// binary RadarPackets out the Grove UART to the hub (CardputerZero) — or, for a
// Phase-5 bench test, straight to the Tab5. The built-in screen is now a STATUS
// display only; the big radar scope lives on the Tab5.
//
// Changes vs. the original single-device project
// (skizzophrenic/Cardputer-CSI-Human-Detector, MIT):
//   * REMOVED the external ILI9341 panel entirely (no more ext_panel.h, no
//     dual-screen rendering, no 3D/scope UI on this device).
//   * KEPT the on-device CSI sensing core UNCHANGED (csiCallback / enableCsi /
//     WiFi join) — this is the guaranteed-working ESP32 CSI path. Attribution
//     for that logic is retained inline and in ../LICENSE.
//   * ADDED radar_link.h (sensor-side state + hold/coast + command parsing) and
//     uart_tx.h (RadarPacket serialisation, shared/radar_protocol.h contract).
//
// Keys (built-in keyboard):  c = calibrate    ` = (reserved)
// =============================================================================
#include <M5Cardputer.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <Preferences.h>
#include <math.h>
#include "esp_wifi.h"

#include "radar_protocol.h"   // shared wire contract (via lib include path)
#include "radar_link.h"
#include "uart_tx.h"

// ── Grove UART pins (to hub / Tab5). Overridable from platformio.ini. ─────────
#ifndef RADAR_TX_PIN
#define RADAR_TX_PIN 2
#endif
#ifndef RADAR_RX_PIN
#define RADAR_RX_PIN 1
#endif
#ifndef RADAR_UART_BAUD
#define RADAR_UART_BAUD 115200
#endif

// Optional compile-time WiFi fallback (from credentials.ini). Saved Preferences
// creds win if present. CSI needs an associated network to sniff.
#ifndef HOME_SSID
#define HOME_SSID ""
#endif
#ifndef HOME_PASS
#define HOME_PASS ""
#endif

// =============================================================================
// On-device CSI sensing  —  reproduced from the reference project (MIT,
// TalkingSasquach). Amplitude + phase-variance motion estimator; the numbers
// and 60/40 blend are the reference's. Do not "improve" casually — this is the
// validated sensing path and the whole point of using the ESP32 for CSI.
// =============================================================================
static const int  kCsiWindow  = 50;
static float      gCsiAmpBuf[kCsiWindow];
static float      gCsiPhaBuf[kCsiWindow];      // mean sin(phase) per frame
static int        gCsiAmpIdx    = 0;
static int        gCsiAmpFilled = 0;
static volatile float    gCsiMotion = 0.0f;
static volatile int8_t   gCsiRssi   = -80;
static volatile uint32_t gCsiCount  = 0;
static float      gCsiVarMax    = 0.001f;
static float      gCsiVarMin    = 0.0f;
static float      gCsiPhaVarMax = 0.001f;
static float      gCsiPhaVarMin = 0.0f;

static void IRAM_ATTR promiscuousRxCb(void*, wifi_promiscuous_pkt_type_t) {}

static void IRAM_ATTR csiCallback(void*, wifi_csi_info_t* info) {
    if (!info || !info->buf || info->len < 4) return;
    gCsiCount++;
    int8_t* b   = info->buf;
    int  nPairs = info->len / 2;

    float ampSum = 0.0f, sinSum = 0.0f;
    int   validPairs = 0;
    for (int i = 0; i < nPairs; i++) {
        float r  = (float)b[2*i];
        float im = (float)b[2*i + 1];
        float amp = sqrtf(r*r + im*im);
        ampSum += amp;
        if (amp > 1e-4f) { sinSum += im / amp; validPairs++; }
    }
    float meanAmp      = ampSum / (float)nPairs;
    float meanSinPhase = validPairs > 0 ? sinSum / (float)validPairs : 0.0f;

    gCsiAmpBuf[gCsiAmpIdx] = meanAmp;
    gCsiPhaBuf[gCsiAmpIdx] = meanSinPhase;
    gCsiAmpIdx = (gCsiAmpIdx + 1) % kCsiWindow;
    if (gCsiAmpFilled < kCsiWindow) gCsiAmpFilled++;
    int n = gCsiAmpFilled;

    float vsum = 0.0f;
    for (int i = 0; i < n; i++) vsum += gCsiAmpBuf[i];
    float vmean = vsum / (float)n;
    float var   = 0.0f;
    for (int i = 0; i < n; i++) { float d = gCsiAmpBuf[i] - vmean; var += d*d; }
    var /= (float)n;

    float psum = 0.0f;
    for (int i = 0; i < n; i++) psum += gCsiPhaBuf[i];
    float pmean = psum / (float)n;
    float pvar  = 0.0f;
    for (int i = 0; i < n; i++) { float d = gCsiPhaBuf[i] - pmean; pvar += d*d; }
    pvar /= (float)n;

    if (gCsiVarMin < 0.0001f) gCsiVarMin = var;
    else gCsiVarMin += (var - gCsiVarMin) * ((var < gCsiVarMin) ? 0.1f : 0.002f);
    if (var > gCsiVarMax) gCsiVarMax = var;
    else gCsiVarMax += (var - gCsiVarMax) * 0.005f;
    float range = gCsiVarMax - gCsiVarMin;
    float ampMotion = (range > 0.0001f) ? ((var - gCsiVarMin) / range) : 0.0f;
    if (ampMotion < 0.0f) ampMotion = 0.0f;
    if (ampMotion > 1.0f) ampMotion = 1.0f;

    if (gCsiPhaVarMin < 0.0001f) gCsiPhaVarMin = pvar;
    else gCsiPhaVarMin += (pvar - gCsiPhaVarMin) * ((pvar < gCsiPhaVarMin) ? 0.1f : 0.002f);
    if (pvar > gCsiPhaVarMax) gCsiPhaVarMax = pvar;
    else gCsiPhaVarMax += (pvar - gCsiPhaVarMax) * 0.005f;
    float prange = gCsiPhaVarMax - gCsiPhaVarMin;
    float phaMotion = (prange > 0.0001f) ? ((pvar - gCsiPhaVarMin) / prange) : 0.0f;
    if (phaMotion < 0.0f) phaMotion = 0.0f;
    if (phaMotion > 1.0f) phaMotion = 1.0f;

    gCsiMotion = 0.6f * ampMotion + 0.4f * phaMotion;   // ref weights
    gCsiRssi   = info->rx_ctrl.rssi;
}

// Reset the normalisation state so a calibration retrains the baseline floors.
static void resetCsi() {
    gCsiAmpIdx = 0; gCsiAmpFilled = 0;
    gCsiVarMax = 0.001f; gCsiVarMin = 0.0f;
    gCsiPhaVarMax = 0.001f; gCsiPhaVarMin = 0.0f;
    memset(gCsiAmpBuf, 0, sizeof(gCsiAmpBuf));
    memset(gCsiPhaBuf, 0, sizeof(gCsiPhaBuf));
    gCsiMotion = 0.0f;
}

static void enableCsi() {
    resetCsi();
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(promiscuousRxCb);
    wifi_csi_config_t cfg = {};
    cfg.lltf_en = true; cfg.htltf_en = true; cfg.stbc_htltf2_en = true;
    cfg.ltf_merge_en = true; cfg.channel_filter_en = true;
    cfg.manu_scale = false; cfg.shift = 0;
    esp_wifi_set_csi_config(&cfg);
    esp_wifi_set_csi_rx_cb(csiCallback, nullptr);
    esp_wifi_set_csi(true);
    wifi_promiscuous_filter_t pf{};
    pf.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&pf);
}

// =============================================================================
// App state
// =============================================================================
static RadarModel  gModel;
static RadarUartTx gTx;
static M5Canvas    gCanvas(&M5Cardputer.Display);   // built-in status buffer

static bool  gWifiReady = false;
static char  gSsid[64]  = {};
static char  gIP[16]    = "---";
static uint32_t gPacketsSent = 0;

static bool loadWifiCreds() {
    Preferences p;
    p.begin("wificreds", true);
    bool ok = p.isKey("ssid");
    if (ok) { p.getString("ssid", gSsid, sizeof(gSsid)); }
    p.end();
    if (ok && gSsid[0]) return true;
    // fall back to compile-time creds
    if (HOME_SSID[0]) { strncpy(gSsid, HOME_SSID, sizeof(gSsid) - 1); return true; }
    return false;
}

// Join WiFi and start CSI. Non-fatal on failure (loop keeps retrying / shows status).
static void connectAndSense() {
    Preferences p;
    char pass[64] = {};
    p.begin("wificreds", true);
    if (p.isKey("pass")) p.getString("pass", pass, sizeof(pass));
    p.end();
    if (!pass[0] && HOME_PASS[0]) strncpy(pass, HOME_PASS, sizeof(pass) - 1);

    WiFi.mode(WIFI_STA);
    if (pass[0]) WiFi.begin(gSsid, pass);
    else         WiFi.begin(gSsid);

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) delay(200);
    if (WiFi.status() != WL_CONNECTED) { gWifiReady = false; return; }

    strncpy(gIP, WiFi.localIP().toString().c_str(), sizeof(gIP) - 1);
    gWifiReady = true;
    enableCsi();
    Serial.printf("# CSI WiFi:%s IP:%s active\n", gSsid, gIP);
}

// =============================================================================
// Status screen (built-in 240x135)
// =============================================================================
static void drawStatus() {
    const int W = gCanvas.width(), H = gCanvas.height();
    gCanvas.fillSprite(TFT_BLACK);

    // title bar
    gCanvas.fillRect(0, 0, W, 14, gCanvas.color565(20, 0, 35));
    gCanvas.setTextSize(1);
    gCanvas.setTextColor(TFT_CYAN);
    const char* t = "[ CSI SENSOR -> UART ]";
    gCanvas.drawString(t, (W - gCanvas.textWidth(t)) / 2, 3);

    const RadarModel::State& s = gModel.state();
    char buf[48];

    // WiFi / link
    gCanvas.setTextColor(gWifiReady ? TFT_GREEN : TFT_RED);
    snprintf(buf, sizeof(buf), "WiFi: %s", gWifiReady ? gSsid : "NO LINK");
    gCanvas.drawString(buf, 4, 20);
    gCanvas.setTextColor(TFT_DARKGREY);
    snprintf(buf, sizeof(buf), "IP %s  RSSI %ddBm", gIP, s.rssi);
    gCanvas.drawString(buf, 4, 32);

    // CSI + calib
    gCanvas.setTextColor(TFT_WHITE);
    const char* cal = (s.calibState == RADAR_CALIB_READY)   ? "READY"
                    : (s.calibState == RADAR_CALIB_CALIBRATING) ? "CALIB" : "IDLE";
    snprintf(buf, sizeof(buf), "CSI frames:%lu  calib:%s",
             (unsigned long)gCsiCount, cal);
    gCanvas.drawString(buf, 4, 46);

    // presence + motion bar
    gCanvas.setTextColor(s.presence ? TFT_GREEN : TFT_DARKGREY);
    gCanvas.drawString(s.presence ? "PRESENCE" : "CLEAR", 4, 62);

    int barX = 4, barY = 76, barW = W - 8, barH = 12;
    gCanvas.drawRect(barX, barY, barW, barH, TFT_DARKGREY);
    int fill = (int)((barW - 2) * s.motion);
    uint16_t c = s.motion > s.threshold ? TFT_GREEN : TFT_BLUE;
    gCanvas.fillRect(barX + 1, barY + 1, fill, barH - 2, c);
    // threshold tick
    int thx = barX + 1 + (int)((barW - 2) * s.threshold);
    gCanvas.drawFastVLine(thx, barY, barH, TFT_YELLOW);

    // motion history sparkline
    int gY = 94, gH = 22;
    gCanvas.drawFastHLine(0, gY + gH, W, gCanvas.color565(30, 30, 30));
    for (int x = 0; x < W; x++) {
        int idx = RadarModel::historySize() - W + x;
        if (idx < 0) continue;
        float v = gModel.historyAt(idx);
        int h = (int)(v * gH);
        gCanvas.drawFastVLine(x, gY + gH - h, h, gCanvas.color565(0, 120, 60));
    }

    // packets
    gCanvas.setTextColor(TFT_DARKGREY);
    snprintf(buf, sizeof(buf), "pkts:%lu  seq:%lu  %dHz  [c]=calib",
             (unsigned long)gPacketsSent, (unsigned long)s.seq, s.rateHz);
    gCanvas.drawString(buf, 4, H - 10);

    gCanvas.pushSprite(0, 0);
}

// =============================================================================
void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(120);
    gCanvas.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());

    Serial.begin(115200);
    gTx.begin(Serial1, RADAR_RX_PIN, RADAR_TX_PIN, RADAR_UART_BAUD);

    if (loadWifiCreds()) connectAndSense();
}

void loop() {
    M5Cardputer.update();

    // keyboard: c = calibrate
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        auto w = M5Cardputer.Keyboard.keysState().word;
        if (!w.empty() && (w[0] == 'c' || w[0] == 'C')) gModel.beginCalibrate();
    }

    // inbound commands from hub/Tab5 (link C)
    gTx.poll(gModel);

    // retry WiFi if we lost the link
    if (!gWifiReady || WiFi.status() != WL_CONNECTED) {
        static uint32_t lastTry = 0;
        if (millis() - lastTry > 5000) { lastTry = millis(); if (gSsid[0]) connectAndSense(); }
    }

    // if a calibration was requested, retrain the CSI baseline once
    if (gModel.consumeResetRequest()) resetCsi();

    // emit one packet at the requested rate
    static uint32_t lastTx = 0;
    uint32_t periodMs = 1000UL / (uint32_t)constrain(gModel.state().rateHz, 1, 50);
    uint32_t now = millis();
    if (now - lastTx >= periodMs) {
        lastTx = now;

        float raw = gWifiReady ? gCsiMotion : 0.0f;
        gModel.update(raw, gCsiRssi);
        const RadarModel::State& s = gModel.state();

        // Synthesize a single blip from the motion-only signal. Single-antenna
        // CSI has no bearing, so angle is fixed at 0; distance is a RELATIVE
        // proxy (more motion -> drawn nearer). See docs/protocol-spec.md.
        RadarBlip blip;
        uint8_t blipCount = 0;
        if (s.presence) {
            blip.angle_deg    = 0.0f;
            blip.distance_est = constrain(1.0f - s.motion, 0.15f, 0.95f);
            blip.confidence   = s.motion;
            blipCount = 1;
        }
        gTx.sendPacket(s, blipCount ? &blip : nullptr, blipCount);
        gPacketsSent++;
    }

    // refresh status screen ~10 Hz
    static uint32_t lastDraw = 0;
    if (now - lastDraw > 100) { lastDraw = now; drawStatus(); }
}
