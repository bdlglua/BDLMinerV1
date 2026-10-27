// Board abstraction: display, buttons, status LED, battery, buzzer, temperature.
// The concrete board is selected at build time (see platformio.ini).
#pragma once

#include <Arduino.h>

#if defined(DISPLAY_M5UNIFIED) || defined(DISPLAY_LGFX_T_DISPLAY) || defined(DISPLAY_LGFX_T_DISPLAY_S3) || \
    defined(DISPLAY_LGFX_CYD) || defined(DISPLAY_LGFX_GAMMA601)
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

// Temperature in degrees Celsius, NAN when the board has no usable sensor
// (or before the first sample). sampleTemperature() reads the sensor: call it
// from the loop task only (the IMU is on an I2C bus other tasks must not use).
float temperature();
void sampleTemperature();
// Stable physical sensor for this session: "imu", "chip" or "unavailable".
const char *temperatureSource();

// 1 = running on battery, 0 = on external power, -1 = unknown or no battery.
// Boards without a power chip (StickC Plus2) are judged by the battery
// voltage: below ~4.05 V it is not being held up by USB.
int onBattery();

// Gravity along the screen's horizontal and vertical axes (rotation 0), in g.
// False without an accelerometer.
bool accel(float &x, float &y);

// The M5 power LED, for short flashes (LED_PIN boards use it for the pool state).
void flashLed(bool on);

}  // namespace board
