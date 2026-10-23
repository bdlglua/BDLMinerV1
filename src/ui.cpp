#include "ui.h"

#include "board.h"

#ifdef HAS_DISPLAY

#include <WiFi.h>

#include "bg/bg.h"
#include "core/work.h"
#include "i18n.h"
#include "market.h"
#include "miner.h"
#include "net.h"
#include "settings.h"

namespace ui {

namespace {

const uint32_t kOrange = 0xF7931A;  // Bitcoin orange: the backgrounds' accent
const uint32_t kGrey = 0x8A8A8A;
const uint32_t kGreen = 0x38D07A;
const uint32_t kRed = 0xE5484D;
const uint32_t kBg = 0x000000;
const uint32_t kFg = 0xFFFFFF;

const uint8_t kBrightnessSteps[] = {100, 60, 30, 10, 0};

// Accent colours of settings.theme: orange, green, blue, purple, pink.
const uint32_t kThemes[] = {kOrange, 0x38D07A, 0x3B9CF6, 0xA66BF5, 0xEC5AA0};
uint32_t accent() { return kThemes[settings.theme % (sizeof(kThemes) / sizeof(kThemes[0]))]; }

lgfx::LovyanGFX *lcd = nullptr;
LGFX_Sprite *canvas = nullptr;
int screen = 0;
uint32_t screenSince = 0;
// mining, clock, global, network, info (bitmap or drawn), then graph,
// records and blocks (always drawn).
const int kScreens = 8;
const int kGraphScreen = 5, kRecordsScreen = 6, kBlocksScreen = 7;
uint8_t backlight = 255;  // what the panel is set to; 0 = off (nothing drawn)
uint32_t flashUntil = 0;  // "+1 share" shown in the header until then

// Hashrate every 10 s for the graph screen (~40 minutes).
float samples[240];
int sampleCount = 0, sampleHead = 0;
uint32_t lastSample = 0;

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
    d.fillRect(0, 0, d.width(), h, accent());
    bodyFont(d);
    d.setTextColor(kBg, accent());
    d.setTextDatum(middle_left);
    d.drawString(title, px(4), h / 2 + 1);

    // Right side: battery and pool link state.
    String right;
    int bat = board::batteryLevel();
    if (bat >= 0) right = String(bat) + "%";
    d.setTextDatum(middle_right);
    int x = d.width() - px(4);
    int r = max(3, px(4));
    d.fillCircle(x - r, h / 2, r, millis() < flashUntil ? 0xFFE14D : stats.poolConnected ? kGreen : kRed);
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
    double hr = minerHashrateLive();
    *unit = "H/s";
    if (hr >= 1e6) {
        hr /= 1e6;
        *unit = "MH/s";
    } else if (hr >= 1e3) {
        hr /= 1e3;
        *unit = "kH/s";
    }
    char num[16];
    // 5 characters: "998.3" kH/s, "1.005" MH/s (2 decimals would only move
    // in 10 kH/s steps around 1 MH/s).
    snprintf(num, sizeof(num), hr >= 100 ? "%.1f" : hr >= 10 ? "%.2f" : "%.3f", hr);
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
    if (!clockNow(t)) return tr("waiting for NTP");
    return trDate(t);
}

String marketAge(const MarketData &m) {
    if (!m.updatedMs) return tr("waiting for prices");
    unsigned min = (millis() - m.updatedMs) / 60000;
    if (!min) return tr("prices just now");
    char buf[40];
    snprintf(buf, sizeof(buf), tr("prices %u min ago"), min);
    return buf;
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
        snprintf(buf, sizeof(buf), tr("%.0f d/block"), years * 365.25);
    } else if (years < 1e4) {
        snprintf(buf, sizeof(buf), tr("%.0f yr/block"), years);
    } else {
        int e = (int)floor(log10(years));
        snprintf(buf, sizeof(buf), tr("%.1fe%d yr/block"), years / pow(10, e), e);
    }
    return buf;
}

// A translated printf format, filled.
String trf(const char *en, unsigned v) {
    char buf[48];
    snprintf(buf, sizeof(buf), tr(en), v);
    return buf;
}

String sharesText() {
    if (!stats.rejected) return String(stats.accepted);
    return String(stats.accepted) + " / " + String(stats.rejected);
}

// ---------------------------------------------------------------------------
// Bitmap background screens
// ---------------------------------------------------------------------------

void drawField(bg::Field f, const String &text, uint32_t color = 0xFFFFFFFF);

void showBackground(bg::Screen s) {
    if (shownBg == s) return;
    lcd->pushImage(0, 0, bgSet->width, bgSet->height, (const lgfx::rgb565_t *)bgSet->pixels[s]);
    shownBg = s;
    for (auto &t : fieldText) t = "\x01";  // force every field to redraw
    // The panel labels, in the selected language.
    for (int f = 0; f < bg::FIELD_COUNT; f++)
        if (bg::kLabel[f] && bg::kScreen[f] == s) drawField((bg::Field)f, tr(bg::kLabel[f]));
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
    if (role == bg::ROLE_LABEL) {
        // Small proportional fonts, like the ones the backgrounds were drawn
        // with; squeezed a little when a translation is longer.
        s.setFont(h >= 12 ? &fonts::DejaVu12 : &fonts::DejaVu9);
        s.setTextSize(1);
        int tw = s.textWidth(text);
        if (tw > w) s.setTextSize(max(0.7f, (float)w / tw), 1);
        return;
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
void drawField(bg::Field f, const String &text, uint32_t color) {
    if (color == 0xFFFFFFFF) color = bg::kColor[f];
    if (color == kOrange) color = accent();
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
    if (millis() < flashUntil) drawField(bg::HDR_STATUS, tr("+1 SHARE"), 0xFFE14D);
    else drawField(bg::HDR_STATUS, tr(stats.poolConnected ? "ONLINE" : "OFFLINE"), stats.poolConnected ? kGreen : kRed);
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
            drawField(bg::MINING_BLOCKS, String(stats.blocksFound), stats.blocksFound ? accent() : kFg);
            drawField(bg::MINING_BITS32, String(stats.over32bits));
            float t = board::temperature();
            drawField(bg::MINING_TEMP, isnan(t) ? String("--") : String((int)lroundf(t)) + " C");
            drawField(bg::MINING_STATE, trStatus(netStatus()));
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
            drawField(bg::GLB_EPOCH, m.retargetBlocks ? trf("retarget: %u blk", m.retargetBlocks) : marketAge(m));
            drawField(bg::GLB_ODDS, soloOddsText());
            break;
        }
        case 3: {
            showBackground(bg::NETWORK);
            drawHeaderFields();
            uint32_t h = stats.blockHeight;
            drawField(bg::NET_HEIGHT, h ? String(h) : String("-"));
            uint32_t halving = (h / 210000 + 1) * 210000;
            drawField(bg::NET_HALVING, h ? trf("halving in %u blocks", halving - h) : String());
            drawField(bg::NET_DIFF, diffText(stats.networkDiff));
            drawField(bg::NET_HASHES, work::formatSI((double)minerTotalHashes(), 2).c_str());
            drawField(bg::NET_POOL, netPoolHost() + ":" + netPoolPort(), netOnBackup() ? accent() : bg::kColor[bg::NET_POOL]);
            drawField(bg::NET_BESTEVER, diffText(stats.bestDiff));
            drawField(bg::NET_IP, "IP " + WiFi.localIP().toString());
            drawField(bg::NET_RSSI, String(WiFi.RSSI()) + " dBm");
            break;
        }
        default: {
            showBackground(bg::INFO);
            drawHeaderFields();
            int hw = 0, sw = 0;
            bool fill = false;
            for (int i = 0; i < stats.minerCount; i++) {
                if (!strcmp(stats.engine[i], "hw")) hw++;
                else if (!strcmp(stats.engine[i], "sw")) sw++;
                else fill = true;  // "fill": software in the hardware loop's idle cycles
            }
            String engines;
            if (hw) engines = String(hw) + tr("x hardware") + (fill ? "+" : "");
            if (sw) engines += String(hw ? " + " : "") + sw + tr("x software");
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
    d.setTextColor(accent(), kBg);
    d.drawString(num, px(6), px(24));
    int numRight = px(6) + d.textWidth(num);

    d.setFont(&fonts::Font4);
    d.setTextSize(scale() < 1 ? 0.6f : scale() * 0.7f);
    d.setTextDatum(bottom_left);
    d.setTextColor(kFg, kBg);
    d.drawString(unit, numRight + px(6), px(24 + 41));

    row(6, 72, tr("Shares"), sharesText(), kGreen);
    row(124, 72, tr("Best"), diffText(stats.sessionBestDiff));
    row(6, 92, "Diff", diffText(stats.poolDiff));
    row(124, 92, tr("Blocks"), String(stats.blocksFound), stats.blocksFound ? accent() : kFg);

    String f = trStatus(netStatus()) + "  " + uptime();
    footer(f.c_str());
}

void drawClock() {
    auto &d = g();
    header(tr("Clock"));
    d.setFont(&fonts::Font7);
    d.setTextSize(scale() * 0.85f);
    d.setTextDatum(top_center);
    d.setTextColor(kFg, kBg);
    d.drawString(timeText(false), d.width() / 2, px(24));

    MarketData m = marketGet();
    bodyFont(d);
    d.setTextColor(kGrey, kBg);
    d.drawString(dateText(), d.width() / 2, px(70));
    row(6, 92, "BTC", m.price > 0 ? priceText(m.price, true) + " " + settings.currency : String("-"), accent());
    row(124, 92, tr("Fee"), m.feeFast ? String(m.feeFast) + " sat/vB" : String("-"));
    String f = String(tr("Block")) + " " + (stats.blockHeight ? String(stats.blockHeight) : String("-")) + "  " + uptime();
    footer(f.c_str());
}

void drawGlobal() {
    header("Global");
    MarketData m = marketGet();
    row(6, 22, "BTC", m.price > 0 ? priceText(m.price, true) + " " + settings.currency : String("-"), accent());
    row(6, 40, tr("Fees"), feesText(m) + " sat/vB");
    row(6, 58, tr("Net"), m.networkHashrate > 0 ? String(work::formatSI(m.networkHashrate, 0).c_str()) + "H/s" : String("-"));
    row(124, 58, "Mempool", m.mempoolTx ? priceText(m.mempoolTx, true) : String("-"));
    row(6, 76, tr("Retarget"), retargetText(m) + (m.retargetBlocks ? tr(" in ") + String(m.retargetBlocks) : String()),
        m.retargetChange >= 0 ? kGreen : kRed);
    row(6, 94, "Solo", soloOddsText());
    footer(marketAge(m).c_str());
}

void drawNetwork() {
    header(tr("Network"));
    row(6, 22, tr("Block"), stats.blockHeight ? String(stats.blockHeight) : String("-"), accent());
    row(6, 40, tr("Net diff"), diffText(stats.networkDiff));
    row(6, 58, netOnBackup() ? tr("Backup") : "Pool", netPoolHost() + ":" + netPoolPort());
    row(6, 76, "IP", WiFi.localIP().toString() + "  " + String(WiFi.RSSI()) + "dBm");
    row(6, 94, tr("Hashes"), work::formatSI((double)minerTotalHashes(), 2).c_str());
    row(124, 94, tr("Best"), diffText(stats.bestDiff));
    String f = String("32-bit shares: ") + stats.over32bits;
    footer(f.c_str());
}

void drawInfo() {
    auto &d = g();
    header("Info");
    row(6, 22, tr("Board"), board::modelName());

    String engines;
    for (int i = 0; i < stats.minerCount; i++) engines += String(i ? "+" : "") + stats.engine[i];
    row(6, 40, tr("Engines"), engines + "  FW " FW_VERSION);

    d.setFont(&fonts::Font0);
    d.setTextSize(scale() < 1 ? 1 : scale());
    d.setTextDatum(top_left);
    d.setTextColor(kGrey, kBg);
    d.drawString(tr("Address"), px(6), px(60));
    d.setTextColor(kFg, kBg);
    // Wrap the address on as many lines as needed.
    String a = settings.btcAddress;
    int perLine = max(1, (int)((d.width() - px(12)) / d.textWidth("W")));
    int y = px(72);
    for (int i = 0; i < (int)a.length() && y < d.height() - px(12); i += perLine, y += d.fontHeight() + 2)
        d.drawString(a.substring(i, i + perLine), px(6), y);

    footer(tr("A:next  B:light  hold A 5s:reset"));
}

// "1 in 2.3M" chance of finding a block within `seconds` at the current rate.
String blockOdds(double seconds) {
    double hr = minerHashrate();
    if (hr <= 0 || stats.networkDiff <= 0) return "-";
    double p = -expm1(-hr * seconds / (stats.networkDiff * 4294967296.0));
    if (p <= 0) return "-";
    return String(tr("1 in ")) + work::formatSI(1 / p, 1).c_str();
}

String duration(double s) {
    char buf[24];
    if (!(s < 1e12)) return "-";
    if (s < 60) snprintf(buf, sizeof(buf), "%.0fs", s);
    else if (s < 3600) snprintf(buf, sizeof(buf), "%.0fmin", s / 60);
    else if (s < 86400) snprintf(buf, sizeof(buf), "%.0fh%02.0f", floor(s / 3600), fmod(s / 60, 60));
    else snprintf(buf, sizeof(buf), tr("%.0fd %.0fh"), floor(s / 86400), fmod(s / 3600, 24));
    return buf;
}

void drawGraph() {
    auto &d = g();
    header("Hashrate");
    int x0 = px(6), y0 = px(24), w = d.width() - px(12), h = d.height() - px(24) - px(16);
    d.drawRect(x0 - 1, y0 - 1, w + 2, h + 2, kGrey);
    if (sampleCount < 2) {
        footer(tr("one point every 10 s"));
        return;
    }
    const int n = sizeof(samples) / sizeof(samples[0]);
    auto at = [&](int i) { return samples[(sampleHead - sampleCount + i + n) % n]; };
    float lo = at(0), hi = lo, sum = 0;
    for (int i = 0; i < sampleCount; i++) {
        lo = min(lo, at(i));
        hi = max(hi, at(i));
        sum += at(i);
    }
    float span = hi - lo > hi * 0.002f ? hi - lo : hi * 0.002f + 1;  // keep a flat line mid-height
    float base = hi - lo > hi * 0.002f ? lo : lo - span / 2;
    int px0 = 0, py0 = 0;
    for (int i = 0; i < sampleCount; i++) {
        int x = x0 + i * (w - 1) / (sampleCount - 1);
        int y = y0 + h - 1 - (int)((at(i) - base) * (h - 1) / span);
        if (i) d.drawLine(px0, py0, x, y, accent());
        px0 = x;
        py0 = y;
    }
    char f[96];
    snprintf(f, sizeof(f), tr("min %s  avg %s  max %sH/s  %s"), work::formatSI(lo, 1).c_str(),
             work::formatSI(sum / sampleCount, 1).c_str(), work::formatSI(hi, 1).c_str(),
             duration(sampleCount * 10.0).c_str());
    footer(f);
}

// The block being mined, then the latest blocks found (mempool.space), each
// with the best share you found while it was the one to find.
void drawBlocks() {
    header(tr("Blocks"));
    MarketData m = marketGet();
    BlockBest bb[BLOCK_BESTS];
    int nb = minerBlockBests(bb, BLOCK_BESTS);
    auto best = [&](uint32_t h) -> String {
        for (int i = 0; i < nb; i++)
            if (bb[i].height == h) return String("  ") + tr("you") + " " + diffText(bb[i].best);
        return String();
    };
    time_t now = time(nullptr);
    String mining = stats.blockHeight ? String(stats.blockHeight) : String("-");
    row(6, 22, mining.c_str(), String(tr("mining")) + best(stats.blockHeight), accent());
    for (int i = 0; i < m.blockCount && i < 4; i++) {
        const MarketData::Block &k = m.blocks[i];
        String ago = now > 1700000000 && k.time ? duration(max<double>(0, now - (double)k.time)) : String("-");
        String pool = String(k.pool[0] ? k.pool : "?").substring(0, 12);
        row(6, 40 + i * 18, String(k.height).c_str(), pool + "  " + ago + best(k.height));
    }
    if (!m.blockCount) footer(tr("no block yet"));
}

void drawRecords() {
    header("Records");
    double hr = minerHashrate();
    row(6, 22, tr("Best ever"), diffText(stats.bestDiff), accent());
    row(124, 22, "Session", diffText(stats.sessionBestDiff));
    row(6, 40, "32-bit", String(stats.over32bits) + "  " + tr("1 per ") + (hr > 0 ? duration(4294967296.0 / hr) : String("-")));
    row(6, 58, tr("Block/day"), blockOdds(86400));
    row(6, 76, tr("Block/year"), blockOdds(31557600));
    row(6, 94, tr("Running"), duration(stats.lifetimeSeconds));
    row(124, 94, tr("Blocks"), String(stats.lifetimeBlocks), stats.lifetimeBlocks ? accent() : kFg);
    BestShare top[3];
    int n = minerTopShares(top, 3);
    String f = String(tr("Hashes")) + " " + work::formatSI((double)stats.lifetimeHashes, 2).c_str() + "  " + tr("top");
    for (int i = 0; i < n; i++) f += " " + diffText(top[i].difficulty);
    footer(f.c_str());
}

}  // namespace

void begin() {
    lcd = board::display();
    if (!lcd) return;
    lcd->setRotation(settings.rotation);
    board::setBrightness(settings.brightness);
    backlight = settings.brightness;
    screenSince = millis();

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
        drawField(bg::SPLASH_INFO, String(board::modelName()) + "  v" FW_VERSION + tr("  -  hold A: setup"));
        return;
    }
    auto &d = g();
    d.fillScreen(kBg);
    d.setFont(&fonts::Font4);
    d.setTextSize(scale());
    d.setTextDatum(middle_center);
    d.setTextColor(accent(), kBg);
    d.drawString("BDLMiner V1", d.width() / 2, d.height() / 2 - px(12));
    d.setFont(&fonts::Font2);
    d.setTextColor(kGrey, kBg);
    d.drawString(String(board::modelName()) + "  v" FW_VERSION, d.width() / 2, d.height() / 2 + px(14));
    d.drawString(tr("Hold A at boot: setup"), d.width() / 2, d.height() - px(10));
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
    d.setTextColor(accent(), kBg);
    d.drawString(tr("WiFi setup"), x, px(6));
    d.setFont(&fonts::Font0);
    d.setTextColor(kFg, kBg);
    int lh = d.fontHeight() + px(4);
    int y = px(30);
    d.drawString(tr("Join WiFi:"), x, y);
    d.drawString(apName, x, y += lh);
    d.drawString(String(tr("Pass: ")) + apPassword, x, y += lh);
    d.drawString(tr("Then open"), x, y += lh * 1.5f);
    d.drawString("192.168.4.1", x, y += lh);
    flush();
}

void showMessage(const char *title, const char *line1, const char *line2) {
    title = tr(title);
    line1 = tr(line1);
    line2 = tr(line2);
    if (!lcd) return;
    if (bgSet) {
        showBackground(bg::SPLASH);
        drawField(bg::SPLASH_INFO, String(title) + ": " + line1 + (line2 ? String(" - ") + line2 : String()), kFg);
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
    screenSince = millis();
    shownBg = -1;
    render();
}

void cycleBrightness() {
    size_t i = 0;
    while (i < sizeof(kBrightnessSteps) && kBrightnessSteps[i] != settings.brightness) i++;
    settings.brightness = kBrightnessSteps[(i + 1) % sizeof(kBrightnessSteps)];
    setBacklight(settings.brightness);
    settingsSave();
}

void setBacklight(uint8_t percent) {
    if (!lcd || percent == backlight) return;
    bool wasOff = backlight == 0;
    backlight = percent;
    board::setBrightness(percent);
    if (wasOff && percent) {
        shownBg = -1;  // nothing was drawn while off
        render();
    }
}

void setRotation(uint8_t r) {
    // Only between the two landscape (or portrait) ways: same panel size.
    if (!lcd || r == lcd->getRotation() || (r & 1) != (lcd->getRotation() & 1)) return;
    lcd->setRotation(r);
    shownBg = -1;
    render();
}

void flashShare() { flashUntil = millis() + 900; }

uint32_t screenAge() { return millis() - screenSince; }

void render() {
    uint32_t now = millis();
    if (now - lastSample >= 10000 || !lastSample) {
        lastSample = now;
        samples[sampleHead] = minerHashrate();
        sampleHead = (sampleHead + 1) % (sizeof(samples) / sizeof(samples[0]));
        if (sampleCount < (int)(sizeof(samples) / sizeof(samples[0]))) sampleCount++;
    }
    if (!lcd || settings.brightness == 0 || backlight == 0) return;
    if (bgSet && screen < kGraphScreen) {
        renderBitmap();
        return;
    }
    if (bgSet) {
        // Drawn straight on the panel (no full-screen buffer with bitmap
        // screens): redraw every 10 s only, which keeps the flicker down.
        static uint32_t drawnAt = 0;
        if (shownBg == -2 && now - drawnAt < 10000) return;
        shownBg = -2;
        drawnAt = now;
    }
    g().fillScreen(kBg);
    switch (screen) {
        case 0: drawMining(); break;
        case 1: drawClock(); break;
        case 2: drawGlobal(); break;
        case 3: drawNetwork(); break;
        case 4: drawInfo(); break;
        case kGraphScreen: drawGraph(); break;
        case kRecordsScreen: drawRecords(); break;
        default: drawBlocks(); break;
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
void setBacklight(uint8_t) {}
void setRotation(uint8_t) {}
void flashShare() {}
uint32_t screenAge() { return 0; }
void render() {}
}  // namespace ui

#endif
