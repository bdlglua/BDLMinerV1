// Screens drawn on boards that have a display. All functions are no-ops on
// headless boards.
#pragma once

#include <Arduino.h>

namespace ui {

void begin();
void showSplash();
void showPortal(const String &apName, const char *apPassword);
void showMessage(const char *title, const char *line1, const char *line2 = nullptr);

void nextScreen();
void cycleBrightness();

// Backlight actually used (night mode, battery saver); 0 stops drawing.
void setBacklight(uint8_t percent);
// Turns the picture (the two landscape ways only), without saving it.
void setRotation(uint8_t rotation);
// Shows "+1 share" in the header for a moment.
void flashShare();
// Milliseconds since the current screen was shown.
uint32_t screenAge();

// Redraws the current screen. Call about once per second.
void render();

}  // namespace ui
