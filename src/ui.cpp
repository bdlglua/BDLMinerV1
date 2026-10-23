#include "ui.h"

#include "board.h"

#ifdef HAS_DISPLAY

#include <WiFi.h>

#include "bg/bg.h"
#include "core/work.h"
#include "market.h"
#include "miner.h"
#include "net.h"
#include "settings.h"

namespace ui {

namespace {

const uint32_t kOrange = 0xF7931A;
const uint32_t kGrey = 0x8A8A8A;
const uint32_t kGreen = 0x38D07A;
const uint32_t kRed = 0xE5484D;
const uint32_t kBg = 0x000000;
const uint32_t kFg = 0xFFFFFF;

const uint8_t kBrightnessSteps[] = {100, 60, 30, 10, 0};

lgfx::LovyanGFX *lcd = nullptr;
LGFX_Sprite *canvas = nullptr;
int screen = 0;
const int kScreens = 5;  // mining, clock, global, network, info

// Bitmap backgrounds (NerdMinerV2 style) when the panel size has a set.
const bg::Set *bgSet = nullptr;
int shownBg = -1;                      // background currently on the panel
String fieldText[bg::FIELD_COUNT];     // what each field shows, to skip redraws
uint32_t fieldColor[bg::FIELD_COUNT];

// Draw target: the off-screen canvas when it could be allocated, otherwise
// the panel directly (some flicker, but works with little RAM).
lgfx::LovyanGFX &g() { return canvas ? (lgfx::LovyanGFX &)*canvas : *lcd; }

void flush() {
    if (canvas) canvas->pushSprite(0, 0);
}

float scale() {
    float s = min(g().width() / 240.0f, g().height() / 135.0f);
    return s < 0.5f ? 0.5f : s;
}

int px(float v) { return (int)(v * scale() + 0.5f); }

// Body text: Font2 scaled up on big panels, the 6x8 font on tiny ones
// (M5StickC, AtomS3...).
void bodyFont(lgfx::LovyanGFX &d) {
    if (scale() < 0.8f) {
        d.setFont(&fonts::Font0);
        d.setTextSize(1);
    } else {
        d.setFont(&fonts::Font2);
        d.setTextSize(max(1.0f, scale()));
    }
}

String uptime() {
    uint32_t s = (millis() - stats.startMillis) / 1000;
    char buf[24];
    snprintf(buf, sizeof(buf), "%ud %02u:%02u:%02u", (unsigned)(s / 86400), (unsigned)(s / 3600 % 24),
             (unsigned)(s / 60 % 60), (unsigned)(s % 60));
    return buf;
}

String diffText(double d) {
    if (d <= 0) return "-";
    if (d < 1) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.5f", d);
        return buf;
    }
    return work::formatSI(d, 2).c_str();
}

void header(const char *title) {
    auto &d = g();
    int h = px(18);
    d.fillRect(0, 0, d.width(), h, kOrange);
    bodyFont(d);
    d.setTextColor(kBg, kOrange);
    d.setTextDatum(middle_left);
    d.drawString(title, px(4), h / 2 + 1);

    // Right side: battery and pool link state.
    String right;
    int bat = board::batteryLevel();
    if (bat >= 0) right = String(bat) + "%";
    d.setTextDatum(middle_right);
    int x = d.width() - px(4);
    int r = max(3, px(4));
    d.fillCircle(x - r, h / 2, r, stats.poolConnected ? kGreen : kRed);
    if (right.length()) d.drawString(right, x - 2 * r - px(4), h / 2 + 1);
}

void footer(const char *text) {
    auto &d = g();
    d.setFont(&fonts::Font0);
    d.setTextSize(scale() < 1 ? 1 : scale());
    d.setTextDatum(bottom_left);
    d.setTextColor(kGrey, kBg);
    d.drawString(text, px(4), d.height() - px(2));
}

// "label  value" row; x/y in 240x135 design units.
void row(float x, float y, const char *label, const String &value, uint32_t color = kFg) {
    auto &d = g();
    bodyFont(d);
    d.setTextDatum(top_left);
    d.setTextColor(kGrey, kBg);
    d.drawString(label, px(x), px(y));
    d.setTextColor(color, kBg);
    d.drawString(value, px(x) + d.textWidth(label) + px(4), px(y));
}

String hashrateText(const char **unit) {
    double hr = minerHashrate();
    *unit = "H/s";
    if (hr >= 1e6) {
        hr /= 1e6;
        *unit = "MH/s";
    } else if (hr >= 1e3) {
        hr /= 1e3;
        *unit = "kH/s";
    }
    char num[16];
    snprintf(num, sizeof(num), hr >= 100 ? "%.1f" : "%.2f", hr);
    return num;
}

// Whole fiat amount; `group` adds thousands separators (the 7-segment font
// used for the big fields has no comma).
String priceText(double price, bool group) {
    if (price <= 0) return "-";
    char raw[24];
    snprintf(raw, sizeof(raw), "%.0f", price);
    if (!group) return raw;
    String out;
    int n = strlen(raw);
    for (int i = 0; i < n; i++) {
        if (i && (n - i) % 3 == 0) out += ',';
        out += raw[i];
    }
    return out;
}

String timeText(bool seconds) {
    struct tm t;
    if (!clockNow(t)) return "--:--";
    char buf[12];
    strftime(buf, sizeof(buf), seconds ? "%H:%M:%S" : "%H:%M", &t);
    return buf;
}

String dateText() {
    struct tm t;
    if (!clockNow(t)) return "waiting for NTP";
    char buf[24];
    strftime(buf, sizeof(buf), "%a %d %b %Y", &t);
    return buf;
}

String marketAge(const MarketData &m) {
    if (!m.updatedMs) return "waiting for prices";
    uint32_t min = (millis() - m.updatedMs) / 60000;
    return "prices " + (min ? String(min) + " min ago" : String("just now"));
}

String feesText(const MarketData &m) {
    if (!m.feeFast) return "-";
    return String(m.feeFast) + " / " + m.feeHalfHour + " / " + m.feeHour;
}

String retargetText(const MarketData &m) {
    if (!m.updatedMs) return "-";
    char buf[16];
    snprintf(buf, sizeof(buf), "%+.2f%%", m.retargetChange);
    return buf;
}

// Expected time for this miner to find a block alone, at the current
// network difficulty: difficulty * 2^32 hashes on average.
String soloOddsText() {
    double hr = minerHashrate();
    if (hr <= 0 || stats.networkDiff <= 0) return "";
    double years = stats.networkDiff * 4294967296.0 / hr / 31557600.0;
    char buf[32];
    if (years < 1) {
        snprintf(buf, sizeof(buf), "%.0f d/block", years * 365.25);
    } else if (years < 1e4) {
        snprintf(buf, sizeof(buf), "%.0f yr/block", years);
    } else {
        int e = (int)floor(log10(years));
        snprintf(buf, sizeof(buf), "%.1fe%d yr/block", years / pow(10, e), e);
    }
    return buf;
}

String sharesText() {
    if (!stats.rejected) return String(stats.accepted);
    return String(stats.accepted) + " / " + String(stats.rejected);
}

// ---------------------------------------------------------------------------
// Bitmap background screens
// ---------------------------------------------------------------------------

void showBackground(bg::Screen s) {
    if (shownBg == s) return;
    lcd->pushImage(0, 0, bgSet->width, bgSet->height, (const lgfx::rgb565_t *)bgSet->pixels[s]);
    shownBg = s;
    for (auto &t : fieldText) t = "\x01";  // force every field to redraw
}

bool isNumeric(const char *t) {
    if (!*t) return false;
    for (; *t; t++)
        if (!strchr("0123456789.:-", *t)) return false;
    return true;
}

// Largest built-in font that fits the box: 7-segment digits for numbers,
// then Font4 / Font2 / Font0.
void fitFont(LGFX_Sprite &s, bg::Role role, const char *text, int w, int h) {
    if (role == bg::ROLE_NUM && isNumeric(text)) {
        s.setFont(&fonts::Font7);
        s.setTextSize(1);
        float k = min((float)h / s.fontHeight(), (float)w / max(1, (int)s.textWidth(text)));
        if (k >= 0.4f) {
            s.setTextSize(k);
            return;
        }
    }
    static const lgfx::IFont *bySize[] = {&fonts::Font4, &fonts::Font2, &fonts::Font0};
    for (const lgfx::IFont *f : bySize) {
        s.setFont(f);
        s.setTextSize(1);
        if (s.fontHeight() <= h + 4 && s.textWidth(text) <= w) return;
    }
}

// Paints `text` into a field: the background under the field is copied into
// a small sprite, the text drawn on it, and the sprite pushed in one go, so
// values update without flicker and without a full-screen buffer.
void drawField(bg::Field f, const String &text, uint32_t color = 0xFFFFFFFF) {
    if (color == 0xFFFFFFFF) color = bg::kColor[f];
    if (fieldText[f] == text && fieldColor[f] == color) return;
    fieldText[f] = text;
    fieldColor[f] = color;

    const bg::Rect &r = bgSet->rects[f];
    LGFX_Sprite s(lcd);
    s.setColorDepth(16);
    if (!s.createSprite(r.w, r.h)) return;
    s.pushImage(-r.x, -r.y, bgSet->width, bgSet->height, (const lgfx::rgb565_t *)bgSet->pixels[shownBg]);
    s.setTextColor(color);

    int x = bg::kAlign[f] == bg::ALIGN_LEFT ? 0 : bg::kAlign[f] == bg::ALIGN_RIGHT ? r.w : r.w / 2;
    s.setTextDatum(bg::kAlign[f] == bg::ALIGN_LEFT    ? middle_left
                   : bg::kAlign[f] == bg::ALIGN_RIGHT ? middle_right
                                                      : middle_center);

    if (bg::kRole[f] == bg::ROLE_WRAP) {
        s.setFont(&fonts::Font2);
        if (s.textWidth(text) > r.w) s.setFont(&fonts::Font0);
        int perLine = max(1, r.w / max(1, (int)s.textWidth("W")));
        int lines = (text.length() + perLine - 1) / perLine;
        int lh = s.fontHeight() + 1;
        int y = r.h / 2 - (lines * lh) / 2 + lh / 2;
        for (int i = 0; i < (int)text.length(); i += perLine, y += lh) s.drawString(text.substring(i, i + perLine), x, y);
    } else {
        fitFont(s, bg::kRole[f], text.c_str(), r.w, r.h);
        s.drawString(text, x, r.h / 2);
    }
    s.pushSprite(r.x, r.y);
}

void drawHeaderFields() {
    int bat = board::batteryLevel();
    drawField(bg::HDR_BATTERY, bat >= 0 ? String(bat) + "%" : String());
    drawField(bg::HDR_STATUS, stats.poolConnected ? "ONLINE" : "OFFLINE", stats.poolConnected ? kGreen : kRed);
}

void renderBitmap() {
    switch (screen) {
        case 0: {
            showBackground(bg::MINING);
            drawHeaderFields();
            const char *unit;
            drawField(bg::MINING_HASHRATE, hashrateText(&unit));
            drawField(bg::MINING_UNIT, unit);
            drawField(bg::MINING_SHARES, sharesText());
            drawField(bg::MINING_BEST, diffText(stats.sessionBestDiff));
            drawField(bg::MINING_POOLDIFF, diffText(stats.poolDiff));
            drawField(bg::MINING_BLOCKS, String(stats.blocksFound), stats.blocksFound ? kOrange : kFg);
            drawField(bg::MINING_BITS32, String(stats.over32bits));
            float t = board::temperature();
            drawField(bg::MINING_TEMP, isnan(t) ? String("--") : String((int)lroundf(t)) + " C");
            drawField(bg::MINING_STATE, netStatus());
            drawField(bg::MINING_UPTIME, uptime());
            break;
        }
        case 1: {
            showBackground(bg::CLOCK);
            drawHeaderFields();
            MarketData m = marketGet();
            const char *unit;
            String hr = hashrateText(&unit);
            drawField(bg::CLOCK_TIME, timeText(false));
            drawField(bg::CLOCK_DATE, dateText());
            drawField(bg::CLOCK_PRICE, m.price > 0 ? priceText(m.price, true) + " " + settings.currency : String("-"));
            drawField(bg::CLOCK_HASHRATE, hr + " " + unit);
            drawField(bg::CLOCK_HEIGHT, stats.blockHeight ? String(stats.blockHeight) : String("-"));
            drawField(bg::CLOCK_FEE, m.feeFast ? String(m.feeFast) : String("-"));
            drawField(bg::CLOCK_SHARES, sharesText());
            drawField(bg::CLOCK_BEST, diffText(stats.sessionBestDiff));
            drawField(bg::CLOCK_STATE, marketAge(m));
            drawField(bg::CLOCK_UPTIME, uptime());
            break;
        }
        case 2: {
            showBackground(bg::GLOBAL);
            drawHeaderFields();
            MarketData m = marketGet();
            drawField(bg::GLB_PRICE, priceText(m.price, false));
            drawField(bg::GLB_SATS, m.price > 0 ? priceText(1e8 / m.price, true) + " sats / " + settings.currency
                                                : settings.currency);
            drawField(bg::GLB_NETHASH, m.networkHashrate > 0 ? String(work::formatSI(m.networkHashrate, 0).c_str()) + "H/s"
                                                             : String("-"));
            drawField(bg::GLB_MEMPOOL, m.mempoolTx ? priceText(m.mempoolTx, true) : String("-"));
            drawField(bg::GLB_FEES, feesText(m));
            drawField(bg::GLB_RETARGET, retargetText(m),
                      !m.updatedMs ? kFg : m.retargetChange >= 0 ? kGreen : kRed);
            drawField(bg::GLB_EPOCH, m.retargetBlocks ? "retarget: " + String(m.retargetBlocks) + " blk" : marketAge(m));
            drawField(bg::GLB_ODDS, soloOddsText());
            break;
        }
        case 3: {
            showBackground(bg::NETWORK);
            drawHeaderFields();
            uint32_t h = stats.blockHeight;
            drawField(bg::NET_HEIGHT, h ? String(h) : String("-"));
            uint32_t halving = (h / 210000 + 1) * 210000;
            drawField(bg::NET_HALVING, h ? "halving in " + String(halving - h) + " blocks" : String());
            drawField(bg::NET_DIFF, diffText(stats.networkDiff));
            drawField(bg::NET_HASHES, work::formatSI((double)minerTotalHashes(), 2).c_str());
            drawField(bg::NET_POOL, netPoolHost() + ":" + netPoolPort(), netOnBackup() ? kOrange : bg::kColor[bg::NET_POOL]);
            drawField(bg::NET_BESTEVER, diffText(stats.bestDiff));
            drawField(bg::NET_IP, "IP " + WiFi.localIP().toString());
            drawField(bg::NET_RSSI, String(WiFi.RSSI()) + " dBm");
            break;
        }
        default: {
            showBackground(bg::INFO);
            drawHeaderFields();
            int hw = 0, sw = 0;
            for (int i = 0; i < stats.minerCount; i++) (strcmp(stats.engine[i], "hw") ? sw : hw)++;
            String engines;
            if (hw) engines = String(hw) + "x hardware";
            if (sw) engines += String(hw ? " + " : "") + sw + "x software";
            drawField(bg::INFO_BOARD, board::modelName());
            drawField(bg::INFO_FIRMWARE, "v" FW_VERSION);
            drawField(bg::INFO_ENGINES, engines);
            drawField(bg::INFO_HEAP, String(ESP.getFreeHeap() / 1024) + " KB");
            drawField(bg::INFO_ADDRESS, settings.btcAddress);
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Procedural screens (panels without a matching background set)
// ---------------------------------------------------------------------------

void drawMining() {
    auto &d = g();
    header("BDLMiner V1");

    const char *unit;
    String num = hashrateText(&unit);

    d.setFont(&fonts::Font7);
    d.setTextSize(scale() * 0.85f);
    d.setTextDatum(top_left);
    d.setTextColor(kOrange, kBg);
    d.drawString(num, px(6), px(24));
    int numRight = px(6) + d.textWidth(num);

    d.setFont(&fonts::Font4);
    d.setTextSize(scale() < 1 ? 0.6f : scale() * 0.7f);
    d.setTextDatum(bottom_left);
    d.setTextColor(kFg, kBg);
    d.drawString(unit, numRight + px(6), px(24 + 41));

    row(6, 72, "Shares", sharesText(), kGreen);
    row(124, 72, "Best", diffText(stats.sessionBestDiff));
    row(6, 92, "Diff", diffText(stats.poolDiff));
    row(124, 92, "Blocks", String(stats.blocksFound), stats.blocksFound ? kOrange : kFg);

    String f = String(netStatus()) + "  " + uptime();
    footer(f.c_str());
}

void drawClock() {
    auto &d = g();
    header("Clock");
    d.setFont(&fonts::Font7);
    d.setTextSize(scale() * 0.85f);
    d.setTextDatum(top_center);
    d.setTextColor(kFg, kBg);
    d.drawString(timeText(false), d.width() / 2, px(24));

    MarketData m = marketGet();
    bodyFont(d);
    d.setTextColor(kGrey, kBg);
    d.drawString(dateText(), d.width() / 2, px(70));
    row(6, 92, "BTC", m.price > 0 ? priceText(m.price, true) + " " + settings.currency : String("-"), kOrange);
    row(124, 92, "Fee", m.feeFast ? String(m.feeFast) + " sat/vB" : String("-"));
    String f = "Block " + (stats.blockHeight ? String(stats.blockHeight) : String("-")) + "  " + uptime();
    footer(f.c_str());
}

void drawGlobal() {
    header("Global");
    MarketData m = marketGet();
    row(6, 22, "BTC", m.price > 0 ? priceText(m.price, true) + " " + settings.currency : String("-"), kOrange);
    row(6, 40, "Fees", feesText(m) + " sat/vB");
    row(6, 58, "Net", m.networkHashrate > 0 ? String(work::formatSI(m.networkHashrate, 0).c_str()) + "H/s" : String("-"));
    row(124, 58, "Mempool", m.mempoolTx ? priceText(m.mempoolTx, true) : String("-"));
    row(6, 76, "Retarget", retargetText(m) + (m.retargetBlocks ? " in " + String(m.retargetBlocks) : String()),
        m.retargetChange >= 0 ? kGreen : kRed);
    row(6, 94, "Solo", soloOddsText());
    footer(marketAge(m).c_str());
}

void drawNetwork() {
    header("Network");
    row(6, 22, "Block", stats.blockHeight ? String(stats.blockHeight) : String("-"), kOrange);
    row(6, 40, "Net diff", diffText(stats.networkDiff));
    row(6, 58, netOnBackup() ? "Backup" : "Pool", netPoolHost() + ":" + netPoolPort());
    row(6, 76, "IP", WiFi.localIP().toString() + "  " + String(WiFi.RSSI()) + "dBm");
    row(6, 94, "Hashes", work::formatSI((double)minerTotalHashes(), 2).c_str());
    row(124, 94, "Best", diffText(stats.bestDiff));
    String f = String("32-bit shares: ") + stats.over32bits;
    footer(f.c_str());
}

void drawInfo() {
    auto &d = g();
    header("Info");
    row(6, 22, "Board", board::modelName());

    String engines;
    for (int i = 0; i < stats.minerCount; i++) engines += String(i ? "+" : "") + stats.engine[i];
    row(6, 40, "Engines", engines + "  FW " FW_VERSION);

    d.setFont(&fonts::Font0);
    d.setTextSize(scale() < 1 ? 1 : scale());
    d.setTextDatum(top_left);
    d.setTextColor(kGrey, kBg);
    d.drawString("Address", px(6), px(60));
    d.setTextColor(kFg, kBg);
    // Wrap the address on as many lines as needed.
    String a = settings.btcAddress;
    int perLine = max(1, (int)((d.width() - px(12)) / d.textWidth("W")));
    int y = px(72);
    for (int i = 0; i < (int)a.length() && y < d.height() - px(12); i += perLine, y += d.fontHeight() + 2)
        d.drawString(a.substring(i, i + perLine), px(6), y);

    footer("A:next  B:light  hold A 5s:reset");
}

}  // namespace

void begin() {
    lcd = board::display();
    if (!lcd) return;
    lcd->setRotation(settings.rotation);
    board::setBrightness(settings.brightness);

    int w = lcd->width(), h = lcd->height();
    bgSet = bg::find(w, h);
    if (bgSet) {
        lcd->fillScreen(kBg);
        return;  // bitmap screens need no full-screen buffer
    }

    // 16-bit canvas when it fits comfortably in RAM, 8-bit otherwise.
    canvas = new LGFX_Sprite(lcd);
    canvas->setColorDepth(w * h * 2 <= 80 * 1024 ? 16 : 8);
    canvas->setPsram(psramFound());
    if (!canvas->createSprite(w, h)) {
        delete canvas;
        canvas = nullptr;
    }
    g().fillScreen(kBg);
    flush();
}

void showSplash() {
    if (!lcd) return;
    if (bgSet) {
        showBackground(bg::SPLASH);
        drawField(bg::SPLASH_INFO, String(board::modelName()) + "  v" FW_VERSION "  -  hold A: setup");
        return;
    }
    auto &d = g();
    d.fillScreen(kBg);
    d.setFont(&fonts::Font4);
    d.setTextSize(scale());
    d.setTextDatum(middle_center);
    d.setTextColor(kOrange, kBg);
    d.drawString("BDLMiner V1", d.width() / 2, d.height() / 2 - px(12));
    d.setFont(&fonts::Font2);
    d.setTextColor(kGrey, kBg);
    d.drawString(String(board::modelName()) + "  v" FW_VERSION, d.width() / 2, d.height() / 2 + px(14));
    d.drawString("Hold A at boot: setup", d.width() / 2, d.height() - px(10));
    flush();
}

void showPortal(const String &apName, const char *apPassword) {
    if (!lcd) return;
    shownBg = -1;
    auto &d = g();
    d.fillScreen(kBg);

    // WiFi QR code: scanning it with a phone joins the setup network.
    String qr = "WIFI:T:WPA;S:" + apName + ";P:" + apPassword + ";;";
    int q = d.height() - px(8);
    if (q > d.width() / 2) q = d.width() / 2;
    d.fillRect(px(4), px(4), q, q, kFg);
    d.qrcode(qr.c_str(), px(4), px(4), q);

    int x = q + px(10);
    bodyFont(d);
    d.setTextDatum(top_left);
    d.setTextColor(kOrange, kBg);
    d.drawString("WiFi setup", x, px(6));
    d.setFont(&fonts::Font0);
    d.setTextColor(kFg, kBg);
    int lh = d.fontHeight() + px(4);
    int y = px(30);
    d.drawString("Join WiFi:", x, y);
    d.drawString(apName, x, y += lh);
    d.drawString(String("Pass: ") + apPassword, x, y += lh);
    d.drawString("Then open", x, y += lh * 1.5f);
    d.drawString("192.168.4.1", x, y += lh);
    flush();
}

void showMessage(const char *title, const char *line1, const char *line2) {
    if (!lcd) return;
    if (bgSet) {
        showBackground(bg::SPLASH);
        drawField(bg::SPLASH_INFO, String(title) + ": " + line1, kFg);
        return;
    }
    auto &d = g();
    d.fillScreen(kBg);
    header(title);
    bodyFont(d);
    d.setTextDatum(middle_center);
    d.setTextColor(kFg, kBg);
    d.drawString(line1, d.width() / 2, d.height() / 2);
    if (line2) {
        d.setTextColor(kGrey, kBg);
        d.drawString(line2, d.width() / 2, d.height() / 2 + px(22));
    }
    flush();
}

void nextScreen() {
    screen = (screen + 1) % kScreens;
    render();
}

void cycleBrightness() {
    size_t i = 0;
    while (i < sizeof(kBrightnessSteps) && kBrightnessSteps[i] != settings.brightness) i++;
    settings.brightness = kBrightnessSteps[(i + 1) % sizeof(kBrightnessSteps)];
    board::setBrightness(settings.brightness);
    settingsSave();
}

void render() {
    if (!lcd || settings.brightness == 0) return;
    if (bgSet) {
        renderBitmap();
        return;
    }
    g().fillScreen(kBg);
    switch (screen) {
        case 0: drawMining(); break;
        case 1: drawClock(); break;
        case 2: drawGlobal(); break;
        case 3: drawNetwork(); break;
        default: drawInfo(); break;
    }
    flush();
}

}  // namespace ui

#else  // headless

namespace ui {
void begin() {}
void showSplash() {}
void showPortal(const String &, const char *) {}
void showMessage(const char *, const char *, const char *) {}
void nextScreen() {}
void cycleBrightness() {}
void render() {}
}  // namespace ui

#endif
