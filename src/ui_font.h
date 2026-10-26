#pragma once
#include <lgfx/v1/LGFXBase.hpp>

namespace ui_font {
// Select a pre-rendered size, never enlarge a pixel font.
const lgfx::IFont *atMost(int height);
}
