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

// Redraws the current screen. Call about once per second.
void render();

}  // namespace ui
