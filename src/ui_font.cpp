#ifndef BDLMINER_DISPLAY_PREVIEW
#include "board.h"
#endif
#if defined(HAS_DISPLAY) || defined(BDLMINER_DISPLAY_PREVIEW)
#include "ui_font.h"
#include "ui_font_data.h"
#include <algorithm>
#include <cstring>

namespace ui_font {
namespace {
class SmoothFont : public lgfx::IFont {
    const Glyph *glyphs_;
    uint8_t height_, baseline_;
    bool numerals_;
    unsigned index(uint16_t code, bool &valid) const {
        if (numerals_) {
            const char *character = code < 128 ? std::strchr(kNumeralCharacters, char(code)) : nullptr;
            valid = character && code >= 32;
            return unsigned((valid ? character : std::strchr(kNumeralCharacters, '?')) - kNumeralCharacters);
        }
        valid = code >= 32 && code <= 126;
        return (valid ? code : '?') - 32;
    }
public:
    constexpr SmoothFont(unsigned first, uint8_t height, uint8_t baseline, bool numerals)
        : glyphs_(kGlyphs + first), height_(height), baseline_(baseline), numerals_(numerals) {}
    int height() const { return height_; }
    void getDefaultMetric(lgfx::FontMetrics *m) const override {
        *m = {glyphs_[0].advance, glyphs_[0].advance, 0, height_, height_, 0, baseline_};
    }
    bool updateFontMetric(lgfx::FontMetrics *m, uint16_t code) const override {
        bool valid;
        const auto &g = glyphs_[index(code, valid)];
        m->width = g.width;
        m->x_advance = g.advance;
        m->x_offset = g.left;
        return valid;
    }
    size_t drawChar(lgfx::LGFXBase *d, int32_t x, int32_t y, uint16_t code,
                    const lgfx::TextStyle *style, lgfx::FontMetrics *, int32_t &) const override {
        bool valid;
        const auto &g = glyphs_[index(code, valid)];
        uint32_t background = style->fore_rgb888 == style->back_rgb888 ? d->getBaseColor() : style->back_rgb888;
        uint32_t shades[16];
        for (int a = 0; a < 16; ++a) {
            uint32_t rgb = 0;
            for (int shift : {16, 8, 0}) {
                int fore = (style->fore_rgb888 >> shift) & 255, back = (background >> shift) & 255;
                rgb |= uint32_t((fore * a + back * (15 - a) + 7) / 15) << shift;
            }
            shades[a] = d->getColorConverter()->convert(rgb);
        }
        d->startWrite();
        unsigned position = 0;
        for (unsigned i = 0; i < g.length; ++i) {
            uint8_t token = kCoverage[g.offset + i];
            unsigned remaining = (token >> 4) + 1, alpha = token & 15;
            while (remaining) {
                unsigned col = position % g.width, row = position / g.width;
                unsigned run = std::min(remaining, unsigned(g.width) - col);
                if (alpha) {
                    int x0 = int((g.left + int(col)) * style->size_x);
                    int x1 = int((g.left + int(col + run)) * style->size_x);
                    int y0 = int(row * style->size_y), y1 = int((row + 1) * style->size_y);
                    d->setRawColor(shades[alpha]);
                    d->writeFillRect(x + x0, y + y0, x1 - x0, y1 - y0);
                }
                position += run;
                remaining -= run;
            }
        }
        d->endWrite();
        return int(g.advance * style->size_x);
    }
};
const SmoothFont kFonts[] = {BDL_FONT_FACES};
#undef BDL_FONT_FACES
}

const lgfx::IFont *atMost(int height, bool numerals) {
    unsigned base = numerals ? kFacesPerFamily : 0;
    for (int i = int(kFacesPerFamily) - 1; i >= 0; --i)
        if (kFonts[base + i].height() <= height) return &kFonts[base + i];
    return &kFonts[base];
}
}
#endif
