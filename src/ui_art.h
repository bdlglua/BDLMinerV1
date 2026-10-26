#pragma once
#include <lgfx/v1/LGFXBase.hpp>

namespace ui_art {
enum Icon { Mining, Clock, Network, Device };
// RGB565 illustrations live in flash. Scaling uses runs of equal pixels;
// it needs no decoded image buffer, PNG decoder or filesystem.
void draw(lgfx::LovyanGFX &target, Icon icon, int x, int y, int w, int h);
}
