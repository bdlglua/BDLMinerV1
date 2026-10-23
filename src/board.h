// Board abstraction: display, buttons, status LED, battery, buzzer, temperature.
// The concrete board is selected at build time (see platformio.ini).
#pragma once

#include <Arduino.h>

#if defined(DISPLAY_M5UNIFIED) || defined(DISPLAY_LGFX_T_DISPLAY) || defined(DISPLAY_LGFX_T_DISPLAY_S3) || \
    defined(DISPLAY_LGFX_CYD)
#define HAS_DISPLAY 1
#include <M5GFX.h>
#endif

#ifndef BOARD_NAME
#define BOARD_NAME "ESP32"
#endif

namespace board {

enum class Input {
    None,
    Next,         // short press: next screen
    Brightness,   // second button short press: brightness / screen off
    FactoryReset, // main button held 5 s
};

void begin();

// True while the main button is held (used at boot to force the portal).
bool mainButtonHeld();

Input poll();

// Display, or nullptr on headless boards / M5 devices without a screen.
#ifdef HAS_DISPLAY
lgfx::LovyanGFX *display();
#endif
const char *modelName();
void setBrightness(uint8_t percent);

// Battery level in percent, or -1 when the board has no battery gauge.
int batteryLevel();

void setLed(bool on);

// Short beep on M5 devices with a speaker or buzzer, or on a passive buzzer
// wired to BUZZER_PIN. Does nothing elsewhere.
void beep();

// Temperature in degrees Celsius, NAN when the board has no usable sensor.
float temperature();

}  // namespace board
