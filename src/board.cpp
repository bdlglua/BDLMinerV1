#include "board.h"

#define LONG_PRESS_MS 5000

// ---------------------------------------------------------------------------
// Displays for third-party boards (M5GFX device definitions)
// ---------------------------------------------------------------------------

#if defined(DISPLAY_LGFX_T_DISPLAY)

#include <lgfx/v1/panel/Panel_ST7789.hpp>

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 panel_;
    lgfx::Bus_SPI bus_;
    lgfx::Light_PWM light_;

public:
    LGFX() {
        auto b = bus_.config();
        b.spi_host = VSPI_HOST;
        b.spi_mode = 0;
        b.freq_write = 40000000;
        b.pin_sclk = 18;
        b.pin_mosi = 19;
        b.pin_miso = -1;
        b.pin_dc = 16;
        bus_.config(b);
        panel_.setBus(&bus_);

        auto p = panel_.config();
        p.pin_cs = 5;
        p.pin_rst = 23;
        p.panel_width = 135;
        p.panel_height = 240;
        p.offset_x = 52;
        p.offset_y = 40;
        p.invert = true;
        p.readable = false;
        panel_.config(p);

        auto l = light_.config();
        l.pin_bl = 4;
        l.pwm_channel = 7;
        light_.config(l);
        panel_.setLight(&light_);
        setPanel(&panel_);
    }
};

#elif defined(DISPLAY_LGFX_T_DISPLAY_S3)

#include <lgfx/v1/panel/Panel_ST7789.hpp>

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 panel_;
    lgfx::Bus_Parallel8 bus_;
    lgfx::Light_PWM light_;

public:
    LGFX() {
        auto b = bus_.config();
        b.freq_write = 20000000;
        b.pin_wr = 8;
        b.pin_rd = 9;
        b.pin_rs = 7;
        const int8_t data[8] = {39, 40, 41, 42, 45, 46, 47, 48};
        for (int i = 0; i < 8; i++) b.pin_data[i] = data[i];
        bus_.config(b);
        panel_.setBus(&bus_);

        auto p = panel_.config();
        p.pin_cs = 6;
        p.pin_rst = 5;
        p.panel_width = 170;
        p.panel_height = 320;
        p.offset_x = 35;
        p.offset_y = 0;
        p.invert = true;
        p.readable = false;
        panel_.config(p);

        auto l = light_.config();
        l.pin_bl = 38;
        l.pwm_channel = 7;
        light_.config(l);
        panel_.setLight(&light_);
        setPanel(&panel_);
    }
};

#elif defined(DISPLAY_LGFX_CYD)

#include <lgfx/v1/panel/Panel_LCD.hpp>

// M5GFX only ships the ILI9342C (M5Stack) driver, so the ILI9341 init
// sequence (same as Adafruit / LovyanGFX) is provided here.
struct Panel_ILI9341 : public lgfx::Panel_LCD {
    Panel_ILI9341() {
        _cfg.memory_width = _cfg.panel_width = 240;
        _cfg.memory_height = _cfg.panel_height = 320;
    }

protected:
    const uint8_t *getInitCommands(uint8_t listno) const override {
        static constexpr uint8_t list0[] = {
            0xEF, 3, 0x03, 0x80, 0x02,
            0xCF, 3, 0x00, 0xC1, 0x30,
            0xED, 4, 0x64, 0x03, 0x12, 0x81,
            0xE8, 3, 0x85, 0x00, 0x78,
            0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,
            0xF7, 1, 0x20,
            0xEA, 2, 0x00, 0x00,
            0xC0, 1, 0x23,        // power control 1
            0xC1, 1, 0x10,        // power control 2
            0xC5, 2, 0x3E, 0x28,  // VCOM control 1
            0xC7, 1, 0x86,        // VCOM control 2
            0xB1, 2, 0x00, 0x18,  // frame rate
            0xB6, 3, 0x08, 0x82, 0x27,
            0xF2, 1, 0x00,        // 3-gamma off
            0x26, 1, 0x01,        // gamma curve 1
            0xE0, 15, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
            0xE1, 15, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,
            CMD_SLPOUT, 0 + CMD_INIT_DELAY, 120,
            CMD_IDMOFF, 0,
            CMD_DISPON, 0 + CMD_INIT_DELAY, 20,
            0xFF, 0xFF,
        };
        return listno == 0 ? list0 : nullptr;
    }
};

class LGFX : public lgfx::LGFX_Device {
    Panel_ILI9341 panel_;
    lgfx::Bus_SPI bus_;
    lgfx::Light_PWM light_;

public:
    LGFX() {
        auto b = bus_.config();
        b.spi_host = HSPI_HOST;
        b.spi_mode = 0;
        b.freq_write = 40000000;
        b.freq_read = 16000000;
        b.pin_sclk = 14;
        b.pin_mosi = 13;
        b.pin_miso = 12;
        b.pin_dc = 2;
        bus_.config(b);
        panel_.setBus(&bus_);

        auto p = panel_.config();
        p.pin_cs = 15;
        p.pin_rst = -1;
        p.panel_width = 240;
        p.panel_height = 320;
        panel_.config(p);

        auto l = light_.config();
        l.pin_bl = 21;
        l.pwm_channel = 7;
        light_.config(l);
        panel_.setLight(&light_);
        setPanel(&panel_);
    }
};

#endif

// ---------------------------------------------------------------------------
// Buttons for non-M5 boards
// ---------------------------------------------------------------------------

#ifndef DISPLAY_M5UNIFIED
namespace {

struct Button {
    int pin = -1;
    bool down = false;
    bool longFired = false;
    uint32_t downAt = 0;

    void begin(int p) {
        pin = p;
        if (pin >= 0) pinMode(pin, INPUT_PULLUP);  // GPIO34-39 rely on external pull-ups
    }
    bool held() const { return pin >= 0 && digitalRead(pin) == LOW; }

    // 1 = click, 2 = long press (fired once while still held)
    int poll() {
        if (pin < 0) return 0;
        bool now = held();
        uint32_t t = millis();
        int ev = 0;
        if (now && !down) {
            downAt = t;
            longFired = false;
        } else if (now && down && !longFired && t - downAt >= LONG_PRESS_MS) {
            longFired = true;
            ev = 2;
        } else if (!now && down && !longFired && t - downAt >= 30) {
            ev = 1;
        }
        down = now;
        return ev;
    }
};

Button btnA, btnB;

}  // namespace
#endif

#ifdef DISPLAY_M5UNIFIED
#include <M5Unified.h>
#endif

namespace board {

#if defined(DISPLAY_LGFX_T_DISPLAY) || defined(DISPLAY_LGFX_T_DISPLAY_S3) || defined(DISPLAY_LGFX_CYD)
static LGFX lcd;
#endif

void begin() {
#ifdef POWER_ON_PIN
    pinMode(POWER_ON_PIN, OUTPUT);  // T-Display-S3: keeps the panel powered on battery
    digitalWrite(POWER_ON_PIN, HIGH);
#endif
#ifdef LED_PIN
    pinMode(LED_PIN, OUTPUT);
    setLed(false);
#endif

#ifdef DISPLAY_M5UNIFIED
    auto cfg = M5.config();
    cfg.serial_baudrate = 0;  // Serial is started by main
    cfg.clear_display = true;
    cfg.internal_imu = true;  // its die temperature is the board's thermometer
    cfg.internal_mic = false;
    cfg.internal_spk = true;  // StickC Plus2: buzzer on GPIO2
    M5.begin(cfg);  // also latches the power-hold pin on the StickC Plus2
    M5.Speaker.setVolume(160);
#else
#ifdef BUTTON_A_PIN
    btnA.begin(BUTTON_A_PIN);
#endif
#ifdef BUTTON_B_PIN
    btnB.begin(BUTTON_B_PIN);
#endif
#endif

#if defined(HAS_DISPLAY) && !defined(DISPLAY_M5UNIFIED)
    lcd.init();
#endif
}

#ifdef HAS_DISPLAY
lgfx::LovyanGFX *display() {
#ifdef DISPLAY_M5UNIFIED
    // Atom Lite/Matrix, Stamp... report a zero sized display.
    return M5.Display.width() > 0 ? &M5.Display : nullptr;
#else
    return &lcd;
#endif
}
#endif

const char *modelName() {
#ifdef DISPLAY_M5UNIFIED
    switch (M5.getBoard()) {
        case m5::board_t::board_M5StickCPlus2: return "M5StickC Plus2";
        case m5::board_t::board_M5StickCPlus: return "M5StickC Plus";
        case m5::board_t::board_M5StickC: return "M5StickC";
        case m5::board_t::board_M5Stack: return "M5Stack Core";
        case m5::board_t::board_M5StackCore2: return "M5Stack Core2";
        case m5::board_t::board_M5Tough: return "M5Tough";
        case m5::board_t::board_M5StackCoreS3: return "M5Stack CoreS3";
        case m5::board_t::board_M5StackCoreS3SE: return "M5Stack CoreS3 SE";
        case m5::board_t::board_M5AtomS3: return "M5AtomS3";
        case m5::board_t::board_M5AtomS3R: return "M5AtomS3R";
        case m5::board_t::board_M5AtomS3Lite: return "M5AtomS3 Lite";
        case m5::board_t::board_M5Cardputer: return "M5Cardputer";
        case m5::board_t::board_M5CardputerADV: return "M5Cardputer ADV";
        case m5::board_t::board_M5Dial: return "M5Dial";
        case m5::board_t::board_M5DinMeter: return "M5DinMeter";
        case m5::board_t::board_M5StickS3: return "M5StickS3";
        case m5::board_t::board_M5AtomLite: return "M5Atom Lite";
        case m5::board_t::board_M5AtomMatrix: return "M5Atom Matrix";
        case m5::board_t::board_M5StampPico: return "M5Stamp Pico";
        case m5::board_t::board_M5StampS3: return "M5StampS3";
        default: return BOARD_NAME;
    }
#else
    return BOARD_NAME;
#endif
}

bool mainButtonHeld() {
#ifdef DISPLAY_M5UNIFIED
    M5.update();
    return M5.BtnA.isPressed();
#else
    return btnA.held();
#endif
}

Input poll() {
#ifdef DISPLAY_M5UNIFIED
    M5.update();
    static bool resetFired = false;
    if (M5.BtnA.pressedFor(LONG_PRESS_MS)) {
        if (!resetFired) {
            resetFired = true;
            return Input::FactoryReset;
        }
    } else if (M5.BtnA.wasReleased()) {
        bool wasLong = resetFired;
        resetFired = false;
        if (!wasLong) return Input::Next;
    }
    if (M5.BtnB.wasClicked() || M5.BtnPWR.wasClicked()) return Input::Brightness;
    return Input::None;
#else
    int a = btnA.poll();
    if (a == 2) return Input::FactoryReset;
    if (a == 1) return Input::Next;
    if (btnB.poll() == 1) return Input::Brightness;
    return Input::None;
#endif
}

void setBrightness(uint8_t percent) {
    uint8_t v = percent * 255 / 100;
#if defined(DISPLAY_M5UNIFIED)
    M5.Display.setBrightness(v);
#elif defined(HAS_DISPLAY)
    lcd.setBrightness(v);
#else
    (void)v;
#endif
}

int batteryLevel() {
#ifdef DISPLAY_M5UNIFIED
    int level = M5.Power.getBatteryLevel();
    return level >= 0 && level <= 100 ? level : -1;
#else
    return -1;
#endif
}

void setLed(bool on) {
#ifdef LED_PIN
#ifdef LED_ACTIVE_LOW
    on = !on;
#endif
    digitalWrite(LED_PIN, on ? HIGH : LOW);
#else
    (void)on;
#endif
}

int onBattery() {
#ifdef DISPLAY_M5UNIFIED
    int vbus = M5.Power.getVBUSVoltage();  // power chips only, else negative
    if (vbus >= 0) return vbus < 4000 ? 1 : 0;
    int mv = M5.Power.getBatteryVoltage();
    if (mv <= 0) return -1;
    return mv < 4050 ? 1 : 0;
#else
    return -1;
#endif
}

bool accel(float &x, float &y) {
#ifdef DISPLAY_M5UNIFIED
    float z;
    return M5.Imu.isEnabled() && M5.Imu.getAccel(&x, &y, &z);
#else
    (void)x;
    (void)y;
    return false;
#endif
}

void flashLed(bool on) {
#if defined(DISPLAY_M5UNIFIED)
    M5.Power.setLed(on ? 255 : 0);
#else
    (void)on;  // LED_PIN already shows the pool state
#endif
}

void beep() {
#if defined(DISPLAY_M5UNIFIED)
    M5.Speaker.tone(4000, 150);
#elif defined(BUZZER_PIN)
    tone(BUZZER_PIN, 4000, 150);
#endif
}

static volatile float lastTemperature = NAN;

void sampleTemperature() {
    float t = NAN;
#ifdef DISPLAY_M5UNIFIED
    if (!M5.Imu.isEnabled() || !M5.Imu.getTemp(&t)) t = NAN;
#endif
    if (isnan(t)) {
        t = temperatureRead();
#if CONFIG_IDF_TARGET_ESP32
        // The original ESP32's sensor is undocumented and missing on most
        // chips, which then always read a raw 128, i.e. 53.3 C.
        if (fabsf(t - (128 - 32) / 1.8f) < 0.01f) t = NAN;
#endif
    }
    lastTemperature = t;
}

float temperature() { return lastTemperature; }

}  // namespace board
