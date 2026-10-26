#pragma once
#include <lgfx/v1/LGFXBase.hpp>

namespace ui_art {
enum Icon { Mining, Clock, Network, Device };
// RGB565 illustrations live in flash. Area filtering smooths their edges
// at the actual panel size, with no decoded image buffer or filesystem.
void draw(lgfx::LovyanGFX &target, Icon icon, int x, int y, int w, int h);
}
