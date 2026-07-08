// =============================================================================
// radar-multi · firmware-tab5  —  display + touch node (M5Stack Tab5, ESP32-P4).
//
// Role: render the full-screen PPI radar scope and take touch input. It receives
// RadarPackets and drives a sweep + contact display; touch buttons send commands
// back upstream (calibrate / threshold / ping — link C).
//
//   Phase 5 (RADAR_DIRECT=1, default here): reads the binary RadarPacket stream
//   straight from the Cardputer's Grove UART — no hub. When the CardputerZero
//   arrives, a JSON reader will feed the same RadarPacket shape from the hub and
//   this render code is unchanged.
//
// Display driver note: the Tab5 panel differs by manufacturing batch (ST7123 vs
// ILI9881-class). M5Unified/M5GFX auto-detect the Tab5 panel, so we use
// M5.Display rather than hand-rolling a panel driver. If your unit shows a blank
// or mirrored panel, update M5Unified and check board = m5stack-tab5.
// =============================================================================
#include <M5Unified.h>
#include <math.h>

#include "radar_protocol.h"   // shared wire contract (-I ../shared)
#include "radar_rx.h"
#include "role.h"

// ── UART to Cardputer / hub. Confirm the real Tab5 Grove GPIOs in hardware-notes.md.
#ifndef RADAR_RX_PIN
#define RADAR_RX_PIN 18
#endif
#ifndef RADAR_TX_PIN
#define RADAR_TX_PIN 17
#endif
#ifndef RADAR_UART_BAUD
#define RADAR_UART_BAUD 115200
#endif

static RadarRx     gRx;
static RoleManager gRole;         // need #5: standalone vs 2nd-screen auto-switch
static RadarPacket gPkt;          // latest decoded packet
static M5Canvas    gCanvas(&M5.Display);
static bool        gHavePkt = false;
static float       gSweepDeg = 0.0f;
static float       gThreshold = 0.15f;

// ── layout (computed at setup from panel size) ───────────────────────────────
static int  W, H;
static int  gScopeCx, gScopeCy, gScopeR;
struct Btn { int x, y, w, h; const char* label; };
static Btn gBtns[4];
static int gSelectedBlip = -1;

static uint16_t dim(uint16_t base, float k) {
    // scale an RGB565 colour toward black by factor k (0..1)
    uint8_t r = ((base >> 11) & 0x1F), g = ((base >> 5) & 0x3F), b = (base & 0x1F);
    r = (uint8_t)(r * k); g = (uint8_t)(g * k); b = (uint8_t)(b * k);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void layout() {
    W = M5.Display.width();
    H = M5.Display.height();
    int panelW = 300;                        // right-hand info/controls column
    int area   = W - panelW;
    gScopeR  = (min(area, H) / 2) - 40;
    gScopeCx = area / 2;
    gScopeCy = H / 2;

    int bx = area + 20, bw = panelW - 40, bh = 70, gap = 20;
    int by = H - (bh + gap) * 4 - 10;
    const char* labels[4] = { "CALIBRATE", "PING", "THR -", "THR +" };
    for (int i = 0; i < 4; i++) {
        gBtns[i] = { bx, by + i * (bh + gap), bw, bh, labels[i] };
    }
}

// =============================================================================
// Rendering
// =============================================================================
static void drawScope() {
    const uint16_t GREEN = gCanvas.color565(0, 255, 120);
    const uint16_t GRID  = gCanvas.color565(0, 60, 30);

    // range rings + crosshair
    for (int k = 1; k <= 4; k++)
        gCanvas.drawCircle(gScopeCx, gScopeCy, gScopeR * k / 4, GRID);
    gCanvas.drawFastHLine(gScopeCx - gScopeR, gScopeCy, gScopeR * 2, GRID);
    gCanvas.drawFastVLine(gScopeCx, gScopeCy - gScopeR, gScopeR * 2, GRID);

    // bearing ticks every 30°
    for (int a = 0; a < 360; a += 30) {
        float rad = (a - 90) * DEG_TO_RAD;
        int x0 = gScopeCx + cosf(rad) * (gScopeR - 12);
        int y0 = gScopeCy + sinf(rad) * (gScopeR - 12);
        int x1 = gScopeCx + cosf(rad) * gScopeR;
        int y1 = gScopeCy + sinf(rad) * gScopeR;
        gCanvas.drawLine(x0, y0, x1, y1, GRID);
    }

    // sweep line + afterglow trail
    for (int t = 0; t < 30; t++) {
        float a = gSweepDeg - t * 2.0f;
        float rad = (a - 90) * DEG_TO_RAD;
        int x = gScopeCx + cosf(rad) * gScopeR;
        int y = gScopeCy + sinf(rad) * gScopeR;
        gCanvas.drawLine(gScopeCx, gScopeCy, x, y, dim(GREEN, (30 - t) / 60.0f));
    }

    // contacts
    if (gHavePkt && !gRx.stale(800)) {
        for (uint8_t i = 0; i < gPkt.blip_count; i++) {
            const RadarBlip& bl = gPkt.blips[i];
            float rad = (bl.angle_deg - 90) * DEG_TO_RAD;
            float rr  = constrain(bl.distance_est, 0.0f, 1.0f) * gScopeR;
            int x = gScopeCx + cosf(rad) * rr;
            int y = gScopeCy + sinf(rad) * rr;
            float conf = constrain(bl.confidence, 0.0f, 1.0f);
            uint16_t c = gCanvas.color565(0, 255, (uint8_t)(120 + 135 * conf));
            gCanvas.fillCircle(x, y, 8 + (int)(10 * conf), c);
            gCanvas.drawCircle(x, y, 8 + (int)(10 * conf) + 6, dim(c, 0.5f));
            if (i == gSelectedBlip) gCanvas.drawCircle(x, y, 28, TFT_WHITE);
        }
    }
}

static void drawPanel() {
    int px = W - 300 + 20;
    char buf[48];
    const uint16_t CY = TFT_CYAN, GY = TFT_DARKGREY;

    gCanvas.setTextSize(2);
    gCanvas.setTextColor(CY);
    gCanvas.drawString("CSI RADAR", px, 20);

    bool live = gHavePkt && !gRx.stale(800);
    gCanvas.setTextColor(live ? TFT_GREEN : TFT_RED);
    gCanvas.drawString(live ? "LINK OK" : "NO LINK", px, 55);

    // role (need #5): which brain/source is driving us right now
    RadarRole role = gRole.current();
    gCanvas.setTextSize(1);
    gCanvas.setTextColor(role == ROLE_SECONDARY_DISPLAY ? TFT_ORANGE
                        : role == ROLE_NO_SOURCE        ? TFT_RED : TFT_GREEN);
    gCanvas.drawString(RoleManager::name(role), px + 130, 62);

    gCanvas.setTextSize(2);
    gCanvas.setTextColor(GY);
    if (gHavePkt) {
        const char* cal = (gPkt.calib_state == RADAR_CALIB_READY) ? "READY"
                        : (gPkt.calib_state == RADAR_CALIB_CALIBRATING) ? "CALIB.." : "IDLE";
        snprintf(buf, sizeof(buf), "calib: %s", cal);
        gCanvas.drawString(buf, px, 95);
        snprintf(buf, sizeof(buf), "contacts: %u", gPkt.blip_count);
        gCanvas.drawString(buf, px, 125);
    }
    snprintf(buf, sizeof(buf), "thr: %.2f", gThreshold);
    gCanvas.drawString(buf, px, 155);
    snprintf(buf, sizeof(buf), "rx ok:%lu crc:%lu",
             (unsigned long)gRx.stats().good, (unsigned long)gRx.stats().badCrc);
    gCanvas.setTextSize(1);
    gCanvas.drawString(buf, px, 185);

    // motion meter
    float m = gHavePkt ? gPkt.motion_intensity : 0.0f;
    int mx = px, my = 205, mw = 260, mh = 22;
    gCanvas.drawRect(mx, my, mw, mh, GY);
    gCanvas.fillRect(mx + 1, my + 1, (int)((mw - 2) * constrain(m, 0.f, 1.f)),
                     mh - 2, gCanvas.color565(0, 200, 100));

    // selected blip info
    if (gSelectedBlip >= 0 && gHavePkt && gSelectedBlip < gPkt.blip_count) {
        const RadarBlip& bl = gPkt.blips[gSelectedBlip];
        gCanvas.setTextSize(2);
        gCanvas.setTextColor(TFT_WHITE);
        snprintf(buf, sizeof(buf), "blip %d", gSelectedBlip);
        gCanvas.drawString(buf, px, 240);
        gCanvas.setTextSize(1);
        gCanvas.setTextColor(GY);
        snprintf(buf, sizeof(buf), "ang %.0f  dist %.2f  conf %.2f",
                 bl.angle_deg, bl.distance_est, bl.confidence);
        gCanvas.drawString(buf, px, 270);
    }
}

static void drawButtons() {
    for (int i = 0; i < 4; i++) {
        const Btn& b = gBtns[i];
        gCanvas.drawRoundRect(b.x, b.y, b.w, b.h, 10, TFT_CYAN);
        gCanvas.setTextSize(2);
        gCanvas.setTextColor(TFT_CYAN);
        int tw = gCanvas.textWidth(b.label);
        gCanvas.drawString(b.label, b.x + (b.w - tw) / 2, b.y + b.h / 2 - 8);
    }
}

static void render() {
    gCanvas.fillSprite(TFT_BLACK);

    // presence banner
    bool present = gHavePkt && gPkt.blip_count > 0 && !gRx.stale(800);
    gCanvas.fillRect(0, 0, W - 300, 40, present ? gCanvas.color565(0, 90, 40)
                                                : gCanvas.color565(30, 0, 0));
    gCanvas.setTextSize(2);
    gCanvas.setTextColor(present ? TFT_GREEN : TFT_RED);
    gCanvas.drawString(present ? "PRESENCE DETECTED" : "AREA CLEAR", 20, 10);
    // when the hub is the brain, flag that this is the full-size 2nd screen
    if (gRole.current() == ROLE_SECONDARY_DISPLAY) {
        gCanvas.setTextColor(TFT_ORANGE);
        const char* s = "HUB DRIVES DISPLAY";
        gCanvas.drawString(s, (W - 300) - gCanvas.textWidth(s) - 20, 10);
    }

    drawScope();
    drawPanel();
    drawButtons();
    gCanvas.pushSprite(0, 0);
}

// =============================================================================
// Touch
// =============================================================================
static bool inRect(int x, int y, const Btn& b) {
    return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

static void handleTouch(int x, int y) {
    // buttons
    for (int i = 0; i < 4; i++) {
        if (!inRect(x, y, gBtns[i])) continue;
        switch (i) {
            case 0: gRx.calibrate(); break;
            case 1: gRx.ping();      break;
            case 2: gThreshold = max(0.02f, gThreshold - 0.02f); gRx.setThreshold(gThreshold); break;
            case 3: gThreshold = min(0.98f, gThreshold + 0.02f); gRx.setThreshold(gThreshold); break;
        }
        return;
    }
    // tap on a contact → select nearest blip within grab radius
    if (gHavePkt) {
        int best = -1; float bestD = 40 * 40;
        for (uint8_t i = 0; i < gPkt.blip_count; i++) {
            const RadarBlip& bl = gPkt.blips[i];
            float rad = (bl.angle_deg - 90) * DEG_TO_RAD;
            float rr  = constrain(bl.distance_est, 0.0f, 1.0f) * gScopeR;
            float bx = gScopeCx + cosf(rad) * rr;
            float by = gScopeCy + sinf(rad) * rr;
            float d = (bx - x) * (bx - x) + (by - y) * (by - y);
            if (d < bestD) { bestD = d; best = i; }
        }
        gSelectedBlip = best;
    }
}

// =============================================================================
void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);
    layout();
    gCanvas.setPsram(true);
    gCanvas.createSprite(W, H);

    Serial.begin(115200);
    gRx.begin(Serial1, RADAR_RX_PIN, RADAR_TX_PIN, RADAR_UART_BAUD);
}

void loop() {
    M5.update();

    // Drain the UART once, routing each byte: binary RadarPackets go to the
    // frame parser; everything else is the hub's ASCII stream (link B heartbeat
    // / future full-size map feed) and drives the role state machine (need #5).
    if (HardwareSerial* u = gRx.uart()) {
        while (u->available()) {
            uint8_t c = (uint8_t)u->read();
            RadarPacket p;
            RadarRx::FeedResult r = gRx.feed(c, p);
            if (r == RadarRx::FEED_PACKET)      { gPkt = p; gHavePkt = true; gRole.notePacket(); }
            else if (r == RadarRx::FEED_NOT_MINE) gRole.feedAscii(c);
        }
    }

    // touch
    if (M5.Touch.isEnabled()) {
        auto t = M5.Touch.getDetail();
        if (t.wasPressed()) handleTouch(t.x, t.y);
    }

    // animate + render ~30 fps
    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last >= 33) {
        last = now;
        gSweepDeg += 6.0f;
        if (gSweepDeg >= 360.0f) gSweepDeg -= 360.0f;
        render();
    }
}
