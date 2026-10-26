#ifndef BDLMINER_DISPLAY_PREVIEW
#include "board.h"
#endif
#if defined(HAS_DISPLAY) || defined(BDLMINER_DISPLAY_PREVIEW)
#include "ui_art.h"
#include "ui_art_data.h"
#include <algorithm>

namespace ui_art {
namespace {
uint16_t filtered(const uint16_t *pixels, int col, int row, int w, int h) {
    // Exact area coverage: average every source pixel touched by this LCD
    // pixel, rather than dropping thin details with nearest-neighbour scaling.
    int left = col * kArtworkSize, right = (col + 1) * kArtworkSize;
    int top = row * kArtworkSize, bottom = (row + 1) * kArtworkSize;
    uint32_t red = 0, green = 0, blue = 0;
    for (int sy = top / h; sy <= (bottom - 1) / h; ++sy) {
        int wy = std::min(bottom, (sy + 1) * h) - std::max(top, sy * h);
        for (int sx = left / w; sx <= (right - 1) / w; ++sx) {
            int wx = std::min(right, (sx + 1) * w) - std::max(left, sx * w);
            uint32_t weight = wx * wy, c = pixels[sy * kArtworkSize + sx];
            red += ((c >> 11) & 31) * weight;
            green += ((c >> 5) & 63) * weight;
            blue += (c & 31) * weight;
        }
    }
    constexpr uint32_t area = kArtworkSize * kArtworkSize;
    return uint16_t(((red + area / 2) / area) << 11 | ((green + area / 2) / area) << 5 | ((blue + area / 2) / area));
}
}
void draw(lgfx::LovyanGFX &d, Icon icon, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    const uint16_t *pixels = kArtwork[static_cast<unsigned>(icon) % 4];
    d.startWrite();
    for (int row = 0; row < h; ++row) {
        int start = 0;
        uint16_t color = filtered(pixels, 0, row, w, h);
        for (int col = 1; col <= w; ++col) {
            uint16_t next = col < w ? filtered(pixels, col, row, w, h) : 0;
            if (col == w || next != color) {
                if (color) d.drawFastHLine(x + start, y + row, col - start, color);
                start = col;
                color = next;
            }
        }
    }
    d.endWrite();
}
}
#endif
