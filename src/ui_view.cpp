#ifndef BDLMINER_DISPLAY_PREVIEW
#include "board.h"
#endif

#if defined(HAS_DISPLAY) || defined(BDLMINER_DISPLAY_PREVIEW)

#include "ui_view.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <new>

namespace ui_view {
namespace {
constexpr size_t kScratchPixels = 4096;

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
        {"LIVE HASHRATE", "HASHRATE"}, {"HASHRATE DIRECT", "HASHRATE"},
        {"BITCOIN PRICE", "BTC"}, {"PRIX DU BITCOIN", "BTC"}, {"BTC PRICE", "BTC"}, {"PRIX BTC", "BTC"},
        {"BEST EVER", "BEST"}, {"LIFE HASHES", "HASH"}, {"RUN TIME", "TIME"}, {"DUREE TOTALE", "DUREE"},
        {"SESSION", "SESS"}, {"BEST DIFF", "BEST"}, {"MEILL. DIFF", "DIFF"},
        {"POWER", "PWR"}, {"PUISSANCE", "PUIS."}, {"LOCAL IP", "IP"}, {"IP LOCALE", "IP"},
        {"NET HASHRATE", "NET"}, {"HASHRATE RESEAU", "RESEAU"}, {"MEMPOOL TX", "TX"}, {"TX MEMPOOL", "TX"},
        {"MINING BLOCK", "BLOCK"}, {"BLOC EN COURS", "BLOC"}, {"SHARES", "SHR"},
        {"32-BIT", "32b"}, {"32 BITS", "32b"}, {"64-BIT", "64b"}, {"64 BITS", "64b"},
    };
    for (const auto &p : pairs) if (label == p[0]) return p[1];
    return label;
}

void labelFont(lgfx::LovyanGFX &d, int size) {
    d.setFont(&lgfx::fonts::Font0);
    d.setTextSize(std::max(1.0f, size / 8.0f));
    d.setTextPadding(0);
}

void fitted(lgfx::LovyanGFX &d, const std::string &value, int x, int y, int w, int h,
            uint32_t color, bool large, bool right = false) {
    if (w <= 0 || h <= 0) return;
    if (h < 14) d.setFont(&lgfx::fonts::Font0);
    else d.setFont(large ? &lgfx::fonts::Font4 : &lgfx::fonts::Font2);
    d.setTextSize(1);
    float factor = std::min(float(h) / d.fontHeight(), float(w) / std::max(1, int(d.textWidth(value.c_str()))));
    if (!large && factor < 0.7f) {
        labelFont(d, 8);
        factor = std::min(1.0f, float(h) / d.fontHeight());
    }
    factor = std::max(factor, 8.0f / d.fontHeight());
    d.setTextSize(factor);
    std::string shown = ellipsis(d, value, w);
    d.setTextColor(color);
    d.setTextDatum(right ? lgfx::middle_right : lgfx::middle_left);
    d.drawString(shown.c_str(), right ? x + w : x, y + h / 2);
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

int View::margin() const { return width_ >= 240 ? 6 : 3; }
int View::headerHeight() const { return height_ >= 200 ? 28 : height_ >= 120 ? 19 : 14; }
int View::footerHeight() const { return height_ >= 200 ? 20 : height_ >= 120 ? 13 : 10; }
Rect View::body() const {
    int m = margin(), y = headerHeight() + m;
    return {m, y, width_ - 2 * m, height_ - footerHeight() - m - y};
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
            draw(strip_, 0, -y);
            strip_.pushSprite(display_, r.x, r.y + y);
        }
    } else {
        display_->setClipRect(r.x, r.y, r.w, r.h);
        display_->fillRect(r.x, r.y, r.w, r.h, background);
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
    int hh = headerHeight(), m = margin();
    display_->fillRoundRect(m, 3, hh - 5, hh - 5, 3, accent_);
    labelFont(*display_, std::max(8, hh - 12));
    display_->setTextDatum(lgfx::middle_center);
    display_->setTextColor(kBackground);
    display_->drawString("B", m + (hh - 5) / 2, 3 + (hh - 5) / 2);
    display_->drawFastHLine(m, hh, width_ - 2 * m, kBorder);
    if (page >= 0) {
        int fy = height_ - footerHeight() / 2, gap = width_ >= 200 ? 6 : 4;
        for (int i = 0; i < kPages; ++i) {
            int x = width_ - m - 2 - (kPages - 1 - i) * gap;
            display_->fillCircle(x, fy, i == page ? 2 : 1, i == page ? accent_ : kBorder);
        }
    }
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

void View::card(int slot, Rect r, const Metric &metric, bool hero, bool wrap) {
    std::string key = metric.label + '\n' + metric.value + '\n' + metric.detail + '\n' + std::to_string(metric.color);
    if (!changed(slot, r, key)) return;
    paint(r, kBackground, [&](lgfx::LovyanGFX &d, int x, int y) {
        int p = wrap && r.h < 40 ? 1 : r.h < 36 ? 3 : std::min(6, std::max(3, r.w / 28));
        int radius = r.h >= 30 ? 5 : 3;
        d.fillRoundRect(x, y, r.w, r.h, radius, kPanel);
        d.drawRoundRect(x, y, r.w, r.h, radius, hero ? accent_ : kBorder);
        if (r.h < 21 && !wrap) {
            int labelW = r.w * 45 / 100;
            labelFont(d, 8);
            d.setTextDatum(lgfx::middle_left);
            d.setTextColor(kMuted);
            d.drawString(ellipsis(d, compactLabel(metric.label), labelW - p).c_str(), x + p, y + r.h / 2);
            fitted(d, metric.value, x + labelW, y + 2, r.w - labelW - p, r.h - 4, metric.color, false, true);
            return;
        }
        int labelH = height_ >= 200 ? 12 : 8;
        labelFont(d, labelH);
        d.setTextColor(kMuted);
        d.setTextDatum(lgfx::top_left);
        std::string label = metric.label;
        if (d.textWidth(label.c_str()) > r.w - 2 * p) label = compactLabel(label);
        bool hideLabel = wrap && r.h < 40;
        if (!hideLabel) d.drawString(ellipsis(d, label, r.w - 2 * p).c_str(), x + p, y + p);
        int vy = hideLabel ? p : p + labelH + 2, vh = r.h - vy - p;
        bool inlineUnit = hero && r.w >= 120 && !metric.detail.empty() && metric.detail.size() <= 5;
        bool detail = !inlineUnit && !metric.detail.empty() && vh >= (hero ? 22 : 26);
        if (detail) vh -= labelH + 3;
        if (wrap) {
            labelFont(d, labelH);
            d.setTextColor(metric.color);
            int perLine = std::max(1, (r.w - 2 * p) / std::max(1, int(d.textWidth("W"))));
            for (size_t i = 0, line = 0; i < metric.value.size() && int((line + 1) * labelH) <= vh; i += perLine, ++line)
                d.drawString(metric.value.substr(i, perLine).c_str(), x + p, y + vy + line * labelH);
        } else {
            int unitW = inlineUnit ? int(metric.detail.size()) * 6 + 4 : 0;
            fitted(d, metric.value, x + p, y + vy, r.w - 2 * p - unitW, vh, metric.color, hero || vh >= 16);
            if (inlineUnit) {
                labelFont(d, 8);
                d.setTextDatum(lgfx::bottom_right);
                d.setTextColor(kMuted);
                d.drawString(metric.detail.c_str(), x + r.w - p, y + r.h - p);
            }
        }
        if (detail) {
            labelFont(d, labelH);
            d.setTextDatum(lgfx::bottom_left);
            d.setTextColor(kMuted);
            d.drawString(ellipsis(d, metric.detail, r.w - 2 * p).c_str(), x + p, y + r.h - p);
        }
    });
}

void View::chrome(const Model &m) {
    int hh = headerHeight(), pad = margin(), statusW = width_ >= 280 ? 104 : width_ >= 200 ? 68 : width_ >= 120 ? 38 : 8;
    int labelH = height_ >= 200 ? 12 : 8;
    text(16, {pad + hh, 2, width_ - 2 * pad - hh - statusW, hh - 3}, m.title, kText, labelH);
    std::string status = m.status;
    if (width_ >= 200 && !m.battery.empty() && status.size() <= 7) status = m.battery + " " + status;
    if (width_ >= 120) text(17, {width_ - pad - statusW, 2, statusW, hh - 3}, status, m.statusColor, labelH, true);
    else {
        Rect dot{width_ - pad - 6, hh / 2 - 3, 6, 6};
        if (changed(17, dot, status + std::to_string(m.statusColor))) paint(dot, kBackground, [&](lgfx::LovyanGFX &d, int x, int y) {
            if (!status.empty()) d.fillCircle(x + 3, y + 3, 2, m.statusColor);
        });
    }
    int dotsW = (width_ >= 200 ? 6 : 4) * kPages + 2;
    text(18, {pad, height_ - footerHeight(), width_ - 2 * pad - dotsW, footerHeight()}, m.footer, kMuted, labelH);
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
            if (previous && p.ms - previousMs <= 20000) d.drawLine(ox + px, oy + py, ox + x, oy + y, accent_);
            else d.fillCircle(ox + x, oy + y, 1, accent_);
            px = x; py = y; previousMs = p.ms; previous = true;
        }
        if (previous) d.fillCircle(ox + px, oy + py, 2, accent_);
    });
}

void View::render(const Model &m) {
    frame(m.page, m.accent, m.language);
    chrome(m);
    Rect b = body();
    int gap = margin() >= 6 ? 5 : 3;
    if (m.page == 5) {
        int summaryH = height_ >= 200 ? 40 : height_ >= 110 ? 24 : 17;
        if (width_ < 180 || height_ < 110) card(1, {b.x, b.y, b.w, summaryH}, m.metrics[1]);
        else {
            int cw = (b.w - 2 * gap) / 3;
            for (int i = 0; i < 3; ++i)
                card(i, {b.x + i * (cw + gap), b.y, i == 2 ? b.w - 2 * (cw + gap) : cw, summaryH}, m.metrics[i]);
        }
        graph(m, {b.x, b.y + summaryH + gap, b.w, b.h - summaryH - gap});
    } else if (m.page == 6) {
        int cw = (b.w - gap) / 2, rh = (b.h - 2 * gap) / 3;
        for (int i = 0; i < 6; ++i)
            card(i, {b.x + (i % 2) * (cw + gap), b.y + (i / 2) * (rh + gap),
                     i % 2 ? b.w - cw - gap : cw, i / 2 == 2 ? b.h - 2 * (rh + gap) : rh}, m.metrics[i], false);
    } else if (m.page == 7) {
        int top = std::max(24, b.h / 3);
        card(0, {b.x, b.y, b.w, top}, m.metrics[0], true);
        int available = b.h - top - gap, count = std::min(4, std::max(1, available / 17));
        int rh = available / count;
        for (int i = 0; i < count; ++i) {
            int y = b.y + top + gap + i * rh;
            const Metric &v = m.metrics[i + 1];
            text(i + 1, {b.x, y, b.w, rh}, v.label + "  " + v.value + (v.detail.empty() ? "" : "  " + v.detail), v.color, 8);
        }
    } else if (m.page == 4 && (height_ < 110 || width_ < 180)) {
        int rh = height_ < 110 ? 10 : 12;
        text(0, {b.x, b.y, b.w, rh}, m.metrics[0].value, kText, 8);
        text(1, {b.x, b.y + rh, b.w, rh}, m.metrics[1].value + " / " + m.metrics[2].value, accent_, 8);
        card(3, {b.x, b.y + 2 * rh + gap, b.w, b.h - 2 * rh - gap}, m.metrics[3], false, true);
    } else if (width_ < 120) {
        int upper = (b.h - 2 * gap) / 2, side = (b.h - upper - 2 * gap) / 2;
        card(0, {b.x, b.y, b.w, upper}, m.metrics[0], true);
        card(1, {b.x, b.y + upper + gap, b.w, side}, m.metrics[1]);
        card(2, {b.x, b.y + upper + side + 2 * gap, b.w, b.h - upper - side - 2 * gap}, m.metrics[2]);
    } else if (height_ < 110) {
        int upper = b.h - 21 - gap;
        card(0, {b.x, b.y, b.w, upper}, m.metrics[0], true);
        int half = (b.w - gap) / 2;
        card(1, {b.x, b.y + upper + gap, half, 21}, m.metrics[1]);
        card(2, {b.x + half + gap, b.y + upper + gap, b.w - half - gap, 21}, m.metrics[2]);
    } else {
        int upper = (b.h - gap) * (m.page == 4 ? (width_ < 180 ? 40 : 50) : 62) / 100;
        int lower = b.h - upper - gap;
        int left = (b.w - gap) * 65 / 100, right = b.w - left - gap;
        int side = (upper - gap) / 2;
        card(0, {b.x, b.y, left, upper}, m.metrics[0], true);
        card(1, {b.x + left + gap, b.y, right, side}, m.metrics[1]);
        card(2, {b.x + left + gap, b.y + side + gap, right, upper - side - gap}, m.metrics[2]);
        if (m.page == 4) card(3, {b.x, b.y + upper + gap, b.w, lower}, m.metrics[3], false, true);
        else {
            int half = (b.w - gap) / 2;
            card(3, {b.x, b.y + upper + gap, half, lower}, m.metrics[3]);
            card(4, {b.x + half + gap, b.y + upper + gap, b.w - half - gap, lower}, m.metrics[4]);
        }
    }
}

void View::message(const char *title, const char *line1, const char *line2) {
    frame(-1, accent_, language_);
    Model m;
    m.title = title ? title : "";
    chrome(m);
    Rect b = body();
    Metric info;
    info.label = title ? title : "";
    info.value = line1 ? line1 : "";
    info.detail = line2 ? line2 : "";
    card(0, b, info, true);
}

void View::portal(const char *title, const char *ssid, const char *password, const char *qr) {
    frame(-2, accent_, language_);
    Model m; m.title = title; m.footer = "192.168.4.1";
    chrome(m);
    Rect b = body();
    int size = std::min(b.h, b.w / 2 - 3);
    if (changed(0, {b.x, b.y, size, size}, qr)) {
        display_->fillRect(b.x, b.y, size, size, uint32_t(0xFFFFFF));
        display_->qrcode(qr, b.x, b.y, size);
    }
    int x = b.x + size + 5, w = b.w - size - 5;
    int rh = b.h / 2;
    Metric ap; ap.label = "WiFi"; ap.value = ssid;
    Metric pass; pass.label = language_ ? "MOT DE PASSE" : "PASSWORD"; pass.value = password;
    card(1, {x, b.y, w, rh - 2}, ap);
    card(2, {x, b.y + rh + 2, w, b.h - rh - 2}, pass);
}

void View::update(const char *title, const char *versions, unsigned percent, const char *state) {
    frame(-3, accent_, language_);
    Model m; m.title = title; m.footer = versions;
    chrome(m);
    Rect b = body();
    Metric info; info.label = title; info.value = state; info.detail = std::to_string(std::min(percent, 100u)) + "%";
    card(0, {b.x, b.y, b.w, b.h - 12}, info, true);
    Rect bar{b.x, b.y + b.h - 7, b.w, 6};
    if (changed(1, bar, std::to_string(percent))) paint(bar, kBorder, [&](lgfx::LovyanGFX &d, int x, int y) {
        d.fillRect(x, y, bar.w * std::min(percent, 100u) / 100, bar.h, accent_);
    });
}

}  // namespace ui_view
#endif
