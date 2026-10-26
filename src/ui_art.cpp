#ifndef BDLMINER_DISPLAY_PREVIEW
#include "board.h"
#endif
#if defined(HAS_DISPLAY) || defined(BDLMINER_DISPLAY_PREVIEW)
#include "ui_art.h"
#include "ui_art_data.h"

namespace ui_art {
void draw(lgfx::LovyanGFX &d, Icon icon, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    const uint16_t *pixels = kArtwork[static_cast<unsigned>(icon) % 4];
    d.startWrite();
    for (int row = 0; row < h; ++row) {
        int sourceRow = row * kArtworkSize / h;
        int start = 0;
        uint16_t color = pixels[sourceRow * kArtworkSize];
        for (int col = 1; col <= w; ++col) {
            uint16_t next = col < w ? pixels[sourceRow * kArtworkSize + col * kArtworkSize / w] : 0;
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
