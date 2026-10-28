#ifndef BDLMINER_DISPLAY_PREVIEW
#include "board.h"
#endif

#if defined(HAS_DISPLAY) || defined(BDLMINER_DISPLAY_PREVIEW)

#include "ui_view.h"
#include "ui_font.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace ui_view {
namespace {
constexpr size_t kScratchPixels = 4096;
constexpr int kLargestNumerals = 80;  // tools/pack_display_fonts.py NUMERAL_SIZES
constexpr int kLargestText = 24;      // TEXT_SIZES

uint32_t blend(uint32_t foreground, uint32_t background, unsigned alpha) {
    uint32_t result = 0;
    for (int shift : {16, 8, 0})
        result |= ((((foreground >> shift) & 255) * alpha + ((background >> shift) & 255) * (255 - alpha) + 127) / 255) << shift;
    return result;
}

void smoothDot(lgfx::LovyanGFX &d, int x, int y, int radius, uint32_t color, uint32_t background = kBackground) {
    for (int dy = -radius; dy <= radius; ++dy) for (int dx = -radius; dx <= radius; ++dx) {
        float coverage = std::max(0.0f, std::min(1.0f, radius + 0.5f - std::sqrt(float(dx * dx + dy * dy))));
        if (coverage > 0) d.drawPixel(x + dx, y + dy, blend(color, background, unsigned(coverage * 255)));
    }
}

void smoothLine(lgfx::LovyanGFX &d, int x0, int y0, int x1, int y1, uint32_t color) {
    bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
    if (steep) { std::swap(x0, y0); std::swap(x1, y1); }
    if (x0 > x1) { std::swap(x0, x1); std::swap(y0, y1); }
    int dx = x1 - x0;
    int32_t position = y0 * 65536;
    int32_t slope = dx ? (y1 - y0) * 65536 / dx : 0;
    for (int x = x0; x <= x1; ++x, position += slope) {
        int y = int(std::floor(position / 65536.0));
        unsigned alpha = unsigned(position - y * 65536) >> 8;
        auto plot = [&](int yy, unsigned a) {
            if (a) d.drawPixel(steep ? yy : x, steep ? x : yy, blend(color, kBackground, a));
        };
        plot(y, 255 - alpha);
        plot(y + 1, alpha);
    }
}

// Use the actual panel fonts for fitting; long addresses and pool names
// remain bounded even on the 80-pixel and square M5 panels.
std::string ellipsis(lgfx::LovyanGFX &d, std::string value, int width) {
    if (d.textWidth(value.c_str()) <= width) return value;
    while (!value.empty() && d.textWidth((value + "...").c_str()) > width) value.pop_back();
    return value.empty() && d.textWidth("...") > width ? "" : value + "...";
}

std::string compactLabel(const std::string &label) {
    static const char *const pairs[][2] = {
        {"FIRMWARE", "FW"}, {"FREE RAM", "RAM"}, {"RAM LIBRE", "RAM"},
        {"NEXT RETARGET", "ADJ."}, {"AJUSTEMENT", "AJ."},
        {"NETWORK DIFF", "DIFF"}, {"DIFF RESEAU", "DIFF"},
        {"TOTAL HASHES", "HASHES"}, {"HASHES TOTAUX", "HASHES"},
        {"BITCOIN PRICE", "BTC"}, {"PRIX DU BITCOIN", "BTC"}, {"BTC PRICE", "BTC"}, {"PRIX BTC", "BTC"},
        {"BEST EVER", "BEST"}, {"LIFE HASHES", "HASH"}, {"RUN TIME", "TIME"}, {"DUREE TOTALE", "DUREE"},
        {"SESSION", "SESS"}, {"BEST DIFF", "BEST"}, {"MEILL. DIFF", "DIFF"},
        {"POWER", "PWR"}, {"PUISSANCE", "PUIS."}, {"LOCAL IP", "IP"}, {"IP LOCALE", "IP"},
        {"NET HASHRATE", "NET"}, {"HASHRATE RESEAU", "RESEAU"}, {"MEMPOOL TX", "TX"}, {"TX MEMPOOL", "TX"},
        {"FEES sat/vB", "FEES"}, {"FRAIS sat/vB", "FRAIS"}, {"HALVING", "HALV."},
        {"SHARES", "SHR"}, {"32-BIT", "32b"}, {"32 BITS", "32b"}, {"64-BIT", "64b"}, {"64 BITS", "64b"},
    };
    for (const auto &p : pairs) if (label == p[0]) return p[1];
    return label;
}

void labelFont(lgfx::LovyanGFX &d, int size) {
    d.setFont(ui_font::atMost(size));
    d.setTextSize(1);
    d.setTextPadding(0);
}

// The largest face, numeral when possible, that fits `value` in w x h.
void fitFont(lgfx::LovyanGFX &d, const std::string &value, int w, int h, int largest) {
    bool numerals = ui_font::numeric(value.c_str());
    int top = std::min(h, numerals ? largest : std::min(largest, kLargestText));
    d.setTextSize(1);
    for (int size = std::max(8, top); size >= 8; --size) {
        d.setFont(ui_font::atMost(size, numerals));
        if (d.textWidth(value.c_str()) <= w) return;
    }
}

int baseline(const lgfx::IFont *font) {
    lgfx::FontMetrics metrics;
    font->getDefaultMetric(&metrics);
    return metrics.baseline;
}

std::string rateLabel(float value) {
    char out[24];
    if (value >= 1e6f) std::snprintf(out, sizeof(out), "%.2fM", double(value / 1e6f));
    else if (value >= 1e3f) std::snprintf(out, sizeof(out), "%.0fk", double(value / 1e3f));
    else std::snprintf(out, sizeof(out), "%.0f", double(value));
    return out;
}
}  // namespace

View::View(lgfx::LovyanGFX *display, bool buffered, uint32_t accent, uint8_t language)
    : display_(display), strip_(display), accent_(accent), language_(language) {
    if (buffered) pixels_.reset(new (std::nothrow) uint16_t[kScratchPixels]);
    strip_.setTextWrap(false);
    display_->setTextWrap(false);
}

void View::invalidate() {
    page_ = -99;
    for (auto &c : cache_) c.valid = false;
    graphValid_ = false;
}

int View::margin() const { return width_ >= 240 ? 8 : width_ >= 120 ? 5 : 3; }
int View::headerHeight() const { return height_ >= 200 ? 24 : height_ >= 120 ? 18 : 13; }
int View::dotsHeight() const { return height_ >= 200 ? 10 : 7; }
int View::labelSize() const { return height_ >= 200 && width_ >= 200 ? 11 : 8; }
Rect View::body() const {
    int m = margin(), y = headerHeight() + (height_ >= 120 ? 4 : 2);
    return {m, y, width_ - 2 * m, height_ - dotsHeight() - y};
}

template <typename Paint>
void View::paint(Rect r, uint32_t background, Paint draw) {
    if (r.w <= 0 || r.h <= 0) return;
    if (pixels_ && size_t(r.w) <= kScratchPixels) {
        int rows = std::max(1, int(kScratchPixels / r.w));
        for (int y = 0; y < r.h; y += rows) {
            int h = std::min(rows, r.h - y);
            strip_.setBuffer(pixels_.get(), r.w, h, 16);
            strip_.fillScreen(background);
            strip_.setBaseColor(background);
            draw(strip_, 0, -y);
            strip_.pushSprite(display_, r.x, r.y + y);
        }
    } else {
        display_->setClipRect(r.x, r.y, r.w, r.h);
        display_->fillRect(r.x, r.y, r.w, r.h, background);
        display_->setBaseColor(background);
        draw(*display_, r.x, r.y);
        display_->clearClipRect();
    }
}

bool View::changed(int slot, Rect rect, const std::string &key) {
    auto &c = cache_[slot];
    if (c.valid && c.key == key && c.rect.x == rect.x && c.rect.y == rect.y && c.rect.w == rect.w && c.rect.h == rect.h)
        return false;
    c.key = key;
    c.rect = rect;
    c.valid = true;
    return true;
}

// Static parts of a page: background, the coin and the page dots. Called on
// every render; it repaints only when the page, panel or accent changes.
void View::frame(int page, uint32_t accent, uint8_t language) {
    if (page_ == page && width_ == display_->width() && height_ == display_->height() &&
        accent_ == accent && language_ == language) return;
    invalidate();
    page_ = page;
    accent_ = accent;
    language_ = language;
    width_ = display_->width();
    height_ = display_->height();
    display_->clearClipRect();
    display_->fillScreen(kBackground);
    int hh = headerHeight(), m = margin(), r = std::max(4, hh / 2 - 2);
    smoothDot(*display_, m + r, hh / 2, r, accent_);
    labelFont(*display_, std::min(16, std::max(8, r * 2 - 2)));
    display_->setBaseColor(accent_);  // antialiasing blends into the coin
    display_->setTextColor(kBackground);
    display_->setTextDatum(lgfx::middle_center);
    display_->drawString("B", m + r + 1, hh / 2 + 1);
    if (page >= 0) {
        int gap = width_ >= 200 ? 9 : 6, y = height_ - dotsHeight() / 2 - 1;
        int x0 = width_ / 2 - (kPages - 1) * gap / 2;
        for (int i = 0; i < kPages; ++i)
            smoothDot(*display_, x0 + i * gap, y, i == page ? 2 : 1, i == page ? accent_ : kBorder);
    }
}

void View::header(const std::string &title, const std::string &status, uint32_t statusColor) {
    int hh = headerHeight(), m = margin(), coin = 2 * std::max(4, hh / 2 - 2) + 5;
    int size = labelSize(), statusW = width_ >= 280 ? 110 : width_ >= 200 ? 80 : width_ >= 120 ? 46 : 10;
    text(16, {m + coin, 0, width_ - 2 * m - coin - statusW, hh}, title, kMuted, size);
    Rect r{width_ - m - statusW, 0, statusW, hh};
    if (!changed(17, r, status + "\n" + std::to_string(statusColor))) return;
    paint(r, kBackground, [&](lgfx::LovyanGFX &d, int x, int y) {
        if (status.empty()) return;
        int dot = hh >= 18 ? 3 : 2;
        if (statusW < 20) { smoothDot(d, x + r.w - dot - 1, y + hh / 2, dot, statusColor); return; }
        labelFont(d, size);
        d.setTextColor(statusColor);
        d.setTextDatum(lgfx::middle_right);
        std::string shown = ellipsis(d, status, r.w - 2 * dot - 5);
        d.drawString(shown.c_str(), x + r.w, y + hh / 2);
        smoothDot(d, x + r.w - d.textWidth(shown.c_str()) - dot - 4, y + hh / 2, dot, statusColor);
    });
}

void View::text(int slot, Rect r, const std::string &value, uint32_t color, int size, bool right) {
    if (!changed(slot, r, value + "\n" + std::to_string(color) + ":" + std::to_string(size))) return;
    paint(r, kBackground, [&](lgfx::LovyanGFX &d, int x, int y) {
        labelFont(d, size);
        d.setTextColor(color);
        d.setTextDatum(right ? lgfx::middle_right : lgfx::middle_left);
        std::string shown = ellipsis(d, value, r.w);
        d.drawString(shown.c_str(), x + (right ? r.w : 0), y + r.h / 2);
    });
}

// The page's one big value, with its unit beside it on the same baseline.
void View::hero(int slot, Rect r, const Metric &metric, bool centered) {
    if (!changed(slot, r, metric.value + '\n' + metric.detail + '\n' + std::to_string(metric.color))) return;
    paint(r, kBackground, [&](lgfx::LovyanGFX &d, int x, int y) {
        int unitSize = std::max(8, std::min(height_ >= 200 ? 18 : 12, r.h / 4));
        labelFont(d, unitSize);
        const lgfx::IFont *unitFont = d.getFont();
        int unitTextW = metric.detail.empty() ? 0 : int(d.textWidth(metric.detail.c_str()));
        int unitH = d.fontHeight(), maxH = r.h - r.h / 8;  // a little air above and below
        // The unit beside the value, or under it when that leaves a bigger
        // value (narrow portrait panels).
        int besideW = unitTextW ? std::min(r.w / 3, unitTextW) + 4 : 0;
        fitFont(d, metric.value, r.w - besideW, maxH, kLargestNumerals);
        const lgfx::IFont *besideFont = d.getFont();
        int besideH = d.fontHeight();
        bool below = false;
        if (unitTextW && maxH > 2 * unitH) {
            fitFont(d, metric.value, r.w, maxH - unitH - 2, kLargestNumerals);
            below = d.fontHeight() > besideH;
        }
        if (!below) d.setFont(besideFont);
        int unitW = below ? 0 : besideW;
        std::string shown = ellipsis(d, metric.value, r.w - unitW);
        int valueW = d.textWidth(shown.c_str()), valueH = d.fontHeight();
        int blockH = valueH + (below ? unitH + 2 : 0);
        int left = x + (centered ? std::max(0, (r.w - valueW - unitW) / 2) : 0);
        int top = y + (r.h - blockH) / 2;
        int base = top + baseline(d.getFont());
        d.setTextColor(metric.color);
        d.setTextDatum(lgfx::top_left);
        d.drawString(shown.c_str(), left, top);
        if (!unitTextW) return;
        d.setFont(unitFont);
        d.setTextColor(kMuted);
        if (below) {
            d.setTextDatum(lgfx::top_center);
            d.drawString(ellipsis(d, metric.detail, r.w).c_str(), centered ? x + r.w / 2 : left + valueW / 2, top + valueH + 2);
            return;
        }
        d.setTextDatum(lgfx::top_left);
        std::string unit = ellipsis(d, metric.detail, unitW - 4);
        d.drawString(unit.c_str(), left + valueW + 4, base - baseline(unitFont));
    });
}

// A secondary reading: label above value (stacked) or "LABEL  value" on one line.
void View::stat(int slot, Rect r, const Metric &metric, bool stacked, bool divider) {
    std::string key = metric.label + '\n' + metric.value + '\n' + std::to_string(metric.color) + (stacked ? "s" : "i");
    if (!changed(slot, r, key)) return;
    paint(r, kBackground, [&](lgfx::LovyanGFX &d, int x, int y) {
        int size = labelSize(), pad = divider ? (width_ >= 200 ? 7 : 4) : 0;
        if (divider) d.drawFastVLine(x, y + 2, r.h - 4, kBorder);
        labelFont(d, size);
        d.setTextColor(kMuted);
        std::string label = metric.label;
        if (stacked) {
            if (d.textWidth(label.c_str()) > r.w - pad) label = compactLabel(label);
            d.setTextDatum(lgfx::top_left);
            d.drawString(ellipsis(d, label, r.w - pad).c_str(), x + pad, y);
            int vy = d.fontHeight() + 2, vh = r.h - vy;
            fitFont(d, metric.value, r.w - pad, std::min(vh, height_ >= 200 ? 24 : 16), 24);
            d.setTextColor(metric.color);
            d.setTextDatum(lgfx::middle_left);
            d.drawString(ellipsis(d, metric.value, r.w - pad).c_str(), x + pad, y + vy + vh / 2);
            return;
        }
        label = compactLabel(label);
        d.setTextDatum(lgfx::middle_left);
        int labelW = std::min(int(d.textWidth(label.c_str())), r.w * 45 / 100);
        d.drawString(ellipsis(d, label, labelW).c_str(), x + pad, y + r.h / 2);
        int valueW = r.w - pad - labelW - 4;
        fitFont(d, metric.value, valueW, r.h, 16);
        d.setTextColor(metric.color);
        d.setTextDatum(lgfx::middle_right);
        d.drawString(ellipsis(d, metric.value, valueW).c_str(), x + r.w, y + r.h / 2);
    });
}

// Up to three readings: side by side on landscape panels, stacked otherwise.
void View::stats(const Model &m, Rect r, int first, int count) {
    bool row = width_ >= height_ && width_ >= 160;
    if (row && width_ < 200) count = std::min(count, 2);
    if (row) {
        int w = r.w / count;
        bool stacked = r.h >= 22;
        for (int i = 0; i < count; ++i)
            stat(first + i, {r.x + i * w, r.y, i == count - 1 ? r.w - i * w : w, r.h}, m.metrics[first + i], stacked, i > 0);
        return;
    }
    int h = r.h / count;
    for (int i = 0; i < count; ++i)
        stat(first + i, {r.x, r.y + i * h, r.w, i == count - 1 ? r.h - i * h : h}, m.metrics[first + i], false, false);
}

void View::graph(const Model &m, Rect r) {
    if (graphValid_ && graphRevision_ == m.graphRevision) return;
    graphValid_ = true;
    graphRevision_ = m.graphRevision;
    paint(r, kBackground, [&](lgfx::LovyanGFX &d, int ox, int oy) {
        int labelW = width_ >= 200 ? 32 : 23;
        int x0 = labelW, y0 = 3, w = r.w - labelW - 3, h = r.h - 15;
        float maximum = 1;
        for (int i = 0; i < m.pointCount; ++i)
            if (std::isfinite(m.points[i].rate)) maximum = std::max(maximum, m.points[i].rate);
        float ceiling = maximum * 1.12f;
        int ticks = h < 24 ? 2 : 3;
        for (int i = 0; i < ticks; ++i) {
            int yy = y0 + i * (h - 1) / (ticks - 1);
            d.drawFastHLine(ox + x0, oy + yy, w, kBorder);
            labelFont(d, 8);
            d.setTextDatum(lgfx::middle_right);
            d.setTextColor(kMuted);
            std::string label = i == ticks - 1 ? "0" : rateLabel(ceiling * (ticks - 1 - i) / (ticks - 1));
            d.drawString(label.c_str(), ox + x0 - 3, oy + yy);
        }
        labelFont(d, 8);
        d.setTextColor(kMuted);
        d.setTextDatum(lgfx::bottom_right);
        d.drawString(language_ ? "maint." : "now", ox + r.w - 3, oy + r.h);
        if (!m.pointCount) return;
        const uint32_t latest = m.points[m.pointCount - 1].ms;
        uint32_t span = latest - m.points[0].ms;
        char age[24];
        std::snprintf(age, sizeof(age), "-%lum%02lus", (unsigned long)(span / 60000), (unsigned long)(span / 1000 % 60));
        d.setTextDatum(lgfx::bottom_left);
        d.drawString(age, ox + x0, oy + r.h);
        bool previous = false;
        int px = 0, py = 0;
        uint32_t previousMs = 0;
        for (int i = 0; i < m.pointCount; ++i) {
            const auto &p = m.points[i];
            if (!std::isfinite(p.rate) || p.rate < 0) { previous = false; continue; }
            int x = x0 + (span ? uint64_t(p.ms - m.points[0].ms) * (w - 1) / span : w - 1);
            int y = y0 + h - 1 - int(p.rate / ceiling * (h - 1));
            if (previous && p.ms - previousMs <= 20000) smoothLine(d, ox + px, oy + py, ox + x, oy + y, accent_);
            else smoothDot(d, ox + x, oy + y, 1, accent_);
            px = x; py = y; previousMs = p.ms; previous = true;
        }
        if (previous) smoothDot(d, ox + px, oy + py, 2, accent_);
    });
}

void View::render(const Model &m) {
    frame(m.page, m.accent, m.language);
    header(m.title, m.status, m.statusColor);
    Rect b = body();
    bool landscape = width_ >= height_ && width_ >= 160;
    int gap = height_ >= 120 ? 4 : 2;
    if (m.page == 5) {
        // Graph: the average as a big value, minimum / maximum beside it.
        int top = height_ >= 200 ? 46 : height_ >= 120 ? 30 : 22;
        int statsW = landscape ? b.w * 45 / 100 : 0;
        hero(0, {b.x, b.y, b.w - statsW, top}, m.metrics[0], false);
        if (statsW) {
            int h = top / 2;
            stat(1, {b.x + b.w - statsW, b.y, statsW, h}, m.metrics[1], false, false);
            stat(2, {b.x + b.w - statsW, b.y + h, statsW, top - h}, m.metrics[2], false, false);
        }
        graph(m, {b.x, b.y + top + gap, b.w, b.h - top - gap});
        return;
    }
    if (m.page == 7) {
        // Blocks: the block being mined, then the latest blocks found.
        int rowH = height_ >= 200 ? 20 : height_ >= 120 ? 15 : 12;
        int rows = std::max(1, std::min(3, (b.h * 45 / 100) / rowH));
        int top = b.h - rows * rowH - gap;
        hero(0, {b.x, b.y, b.w, top}, m.metrics[0]);
        for (int i = 0; i < rows; ++i) {
            const Metric &v = m.metrics[i + 1];
            std::string line = v.label + "  " + v.value + (v.detail.empty() ? "" : "  " + v.detail);
            text(i + 1, {b.x, b.y + top + gap + i * rowH, b.w, rowH}, v.label.empty() ? "" : line, v.color, labelSize());
        }
        return;
    }
    int count = 3;
    int statsH = landscape ? (height_ >= 200 ? 44 : height_ >= 120 ? 30 : 13)
                           : count * (height_ >= 200 ? 20 : height_ >= 140 ? 16 : 13);
    if (landscape && width_ < 200) count = 2;
    hero(0, {b.x, b.y, b.w, b.h - statsH - gap}, m.metrics[0]);
    stats(m, {b.x, b.y + b.h - statsH, b.w, statsH}, 1, count);
}

void View::message(const char *title, const char *line1, const char *line2) {
    bool splash = title && std::string(title) == "BDLMiner V1";
    frame(splash ? -4 : -1, accent_, language_);
    header(title ? title : "", "", kMuted);
    Rect b = body();
    int lineH = labelSize() + 6;
    Metric big;
    big.value = line1 ? line1 : "";
    big.color = splash ? accent_ : kText;
    hero(0, {b.x, b.y, b.w, b.h - lineH}, big);
    text(1, {b.x, b.y + b.h - lineH, b.w, lineH}, line2 ? line2 : "", kMuted, labelSize());
}

void View::portal(const char *title, const char *ssid, const char *password, const char *qr) {
    frame(-2, accent_, language_);
    header(title, "192.168.4.1", kMuted);
    Rect b = body();
    int size = std::min(b.h, b.w / 2 - 3);
    if (changed(0, {b.x, b.y, size, size}, qr)) {
        display_->fillRect(b.x, b.y, size, size, uint32_t(0xFFFFFF));
        display_->qrcode(qr, b.x, b.y, size);
    }
    int x = b.x + size + 6, w = b.w - size - 6, h = b.h / 2;
    Metric ap; ap.label = "WiFi"; ap.value = ssid;
    Metric pass; pass.label = language_ ? "MOT DE PASSE" : "PASSWORD"; pass.value = password; pass.color = accent_;
    stat(1, {x, b.y, w, h}, ap, true, false);
    stat(2, {x, b.y + h, w, b.h - h}, pass, true, false);
}

void View::update(const char *title, const char *versions, unsigned percent, const char *state) {
    frame(-3, accent_, language_);
    header(title, state, accent_);
    Rect b = body();
    unsigned shown = std::min(percent, 100u);
    int barH = height_ >= 200 ? 8 : 5, lineH = labelSize() + 6;
    Metric big;
    big.value = std::to_string(shown);
    big.detail = "%";
    big.color = accent_;
    hero(0, {b.x, b.y, b.w, b.h - lineH - barH - 4}, big);
    text(1, {b.x, b.y + b.h - lineH - barH - 2, b.w, lineH}, versions, kMuted, labelSize());
    Rect bar{b.x, b.y + b.h - barH, b.w, barH};
    if (changed(2, bar, std::to_string(shown))) paint(bar, kBorder, [&](lgfx::LovyanGFX &d, int x, int y) {
        d.fillRect(x, y, bar.w * int(shown) / 100, bar.h, accent_);
    });
}

}  // namespace ui_view
#endif
