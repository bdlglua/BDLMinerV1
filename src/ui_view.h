// Device-independent display rendering. The same code and LovyanGFX fonts
// are used by the firmware and the native display previews.
#pragma once

#include <lgfx/v1/LGFX_Sprite.hpp>
#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace ui_view {

constexpr int kPages = 8;
constexpr uint32_t kBackground = 0x0B1017, kPanel = 0x151D28;
constexpr uint32_t kBorder = 0x2A3646, kText = 0xF1F5FA, kMuted = 0x94A3B8;
constexpr uint32_t kGreen = 0x42D99B, kRed = 0xFF6577, kAmber = 0xFFBC55;

struct Rect { int x, y, w, h; };
struct Point { uint32_t ms; float rate; };
struct Metric {
    std::string label, value, detail;
    uint32_t color = kText;
};
struct Model {
    int page = 0;
    uint8_t language = 0;
    uint32_t accent = 0xF7931A, statusColor = kGreen;
    std::string title, status, battery, footer;
    std::array<Metric, 6> metrics;
    const Point *points = nullptr;  // chronological; borrowed during render()
    int pointCount = 0;
    uint32_t graphRevision = 0;
};

class View {
public:
    // Allocates at most 8 KiB once; every field is painted in small strips.
    // With no scratch buffer, clipped direct drawing remains available.
    explicit View(lgfx::LovyanGFX *display, bool buffered = true,
                  uint32_t accent = 0xF7931A, uint8_t language = 0);
    void invalidate();
    void render(const Model &model);
    void message(const char *title, const char *line1, const char *line2 = nullptr);
    void portal(const char *title, const char *ssid, const char *password, const char *qr);
    void update(const char *title, const char *versions, unsigned percent, const char *state);

private:
    struct Cached { std::string key; Rect rect{}; bool valid = false; };
    lgfx::LovyanGFX *display_;
    lgfx::LGFX_Sprite strip_;
    std::unique_ptr<uint16_t[]> pixels_;
    std::array<Cached, 24> cache_;
    int page_ = -99, width_ = 0, height_ = 0;
    uint32_t accent_ = 0xF7931A;
    uint8_t language_ = 0;
    uint32_t graphRevision_ = 0;
    bool graphValid_ = false;

    int margin() const;
    int headerHeight() const;
    int footerHeight() const;
    Rect body() const;
    bool changed(int slot, Rect rect, const std::string &key);
    void frame(int page, uint32_t accent, uint8_t language);
    void text(int slot, Rect rect, const std::string &value, uint32_t color, int size, bool right = false);
    void card(int slot, Rect rect, const Metric &metric, bool hero = false, bool wrap = false);
    void chrome(const Model &model);
    void graph(const Model &model, Rect rect);
    template <typename Paint> void paint(Rect rect, uint32_t background, Paint draw);
};

}  // namespace ui_view
