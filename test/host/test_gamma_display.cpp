// Compile the actual Gamma UI with real font metrics and a monochrome sprite.
#include "../../src/ui_gamma.cpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>
Settings settings;
MinerStats demoStats{};
gamma601::Status demoAsic{};
float demoTemperature = 52.1f;
double demoRate = 1.2e12;
lgfx::LGFX_Sprite screen;
namespace board { lgfx::LovyanGFX *display() { return &screen; } }
namespace gamma601 {
Status status() { return demoAsic; }
float temperature() { return demoTemperature; }
}
MinerStats minerStats() { return demoStats; }
double minerHashrateLive() { return demoRate; }
uint8_t settingsBrightness() { return settings.brightness; }
void settingsSetBrightness(uint8_t b) { settings.brightness = b; }
std::vector<uint32_t> pixels() {
    std::vector<uint32_t> out;
    for (int y = 0; y < screen.height(); ++y)
        for (int x = 0; x < screen.width(); ++x) out.push_back(screen.readPixel(x, y));
    return out;
}
void save(const std::string &directory, const char *name) {
    if (directory.empty()) return;
    std::ofstream file(directory + "/" + name + ".ppm", std::ios::binary);
    file << "P6\n128 32\n255\n";
    for (auto pixel : pixels()) {
        char level = pixel ? 255 : 0;
        char rgb[]{level, level, level}; file.write(rgb, 3);
    }
}
int main(int argc, char **argv) {
    std::string out = argc > 1 ? argv[1] : "";
    settings.rotation = 0;
    screen.setColorDepth(1); assert(screen.createSprite(128, 32)); ui::begin();
    assert(screen.width() == 128 && screen.height() == 32);
    demoStats.accepted = 42; demoStats.rejected = 0; demoStats.poolConnected = true; demoStats.bestDiff = 1234567;
    demoAsic.running = true; demoAsic.state = "Mining"; demoAsic.rpm = 4800;
    demoAsic.inputVoltage = 5.08; demoAsic.coreVoltage = 1.15; demoAsic.current = 15.2;
    demoAsic.frequency = 600; demoAsic.regulatorTemperature = 48.3;
    const char *names[]{"mining", "power", "pool"};
    for (unsigned i = 0; i < 3; ++i) {
        ui::page = i; ui::render(); save(out, names[i]);
        assert(screen.fontHeight() == 8);
    }
    std::cout << "Gamma OLED: " << screen.width() << 'x' << screen.height()
              << ", font height " << screen.fontHeight() << ", 21 characters " << screen.textWidth("123456789012345678901") << "px\n";
    ui::page = 0; demoStats.accepted = UINT32_MAX; demoStats.rejected = UINT32_MAX;
    ui::render(); save(out, "large-counts");
    demoAsic.running = false; demoAsic.state = "Gamma power limit must be 10-100% or paused";
    ui::render(); save(out, "fault");
    ui::blockFound(950128); save(out, "block"); assert(ui::dismissAlert());
    // With ten accepted digits the old layout hid even a one-digit rejection.
    demoAsic.running = true; ui::page = 0; demoStats.rejected = 1; ui::render(); auto first = pixels();
    demoStats.rejected = 2; ui::render(); assert(pixels() != first);
    assert(ui::counter(999999) == "999999" && ui::counter(1000000) == "1.00M");
    String totals = String("A:") + ui::counter(UINT32_MAX) + " R:" + ui::counter(UINT32_MAX);
    assert(screen.textWidth(totals.c_str()) <= 126);
    // Every row stays in its own eight-pixel band with one-pixel side margins.
    for (uint8_t rotation : {uint8_t(0), uint8_t(2)}) {
        ui::setRotation(rotation); assert(screen.width() == 128 && screen.height() == 32);
        for (unsigned row = 0; row < 4; ++row) {
            String text[4]; text[row] = "Ag19";
            screen.setFont(&lgfx::fonts::Font2); // another caller must not change the row geometry
            ui::lines(text[0], text[1], text[2], text[3]); assert(screen.fontHeight() == 8);
            bool lit = false;
            for (int y = 0; y < 32; ++y) for (int x = 0; x < 128; ++x) {
                if (screen.readPixel(x,y)) {
                    lit = true;
                    assert(y >= int(row * 8) && y < int(row * 8 + 8));
                    assert(x > 0 && x < 127);
                }
            }
            assert(lit);
        }
    }
    ui::setRotation(0);
    // Incoming newlines/tabs must never spill onto the next row.
    ui::lines("one\ntwo\tthree", "", "", "");
    for (int y = 8; y < 32; ++y) for (int x = 0; x < 128; ++x) assert(!screen.readPixel(x,y));
    // Paging preserves the entire string and UTF-8 boundaries, including
    // unbroken SSIDs and the tail containing an important restart instruction.
    for (const char *message : {"Gamma power limit must be 10-100% or paused",
                              "Fan control I2C fault; restart required",
                              "SSID-12345678901234567890123456789012",
                              u8"Reseau-éééééééééééééééééééééééé"}) {
        String original(message), reconstructed;
        unsigned parts = 0;
        for (unsigned start = 0; start < original.length(); ++parts) start = ui::rowEnd(original, start, 126);
        for (unsigned part = 0; part < parts; ++part) {
            String segment = ui::rowWindow(original, 126, part * 3000);
            assert(screen.textWidth(segment.c_str()) <= 126);
            assert(segment.isEmpty() || (uint8_t(segment[0]) & 0xc0) != 0x80);
            reconstructed += segment;
        }
        assert(reconstructed == original);
    }
    demoAsic.running = false; demoAsic.state = "Gamma power limit must be 10-100% or paused";
    ui::render(); first = pixels(); testMillis += 3000; ui::render();
    assert(pixels() != first); save(out, "fault-detail");
    demoAsic.state = "Fan control I2C fault; restart required"; ui::render();
    assert(std::string(ui::longRows[1].c_str()) == demoAsic.state && ui::rowStarted[1] == testMillis);
    testMillis = UINT32_MAX - 1000; ui::longRows[1] = ""; ui::render(); first = pixels();
    testMillis += 3000; ui::render(); assert(pixels() != first);
    // Unavailable readings stay short and explicit on the power page.
    demoAsic.running = true; demoAsic.inputVoltage = demoAsic.coreVoltage = demoAsic.current = NAN;
    assert(ui::reading(NAN, 3) == "--" && ui::reading(INFINITY, 1) == "--");
    ui::page = 1; ui::render(); save(out, "no-sensors");
    assert(screen.width() == 128 && screen.height() == 32);
    ui::showSplash(); save(out, "startup");
    ui::showPortal("BDLMinerV1-very-long-device-label", "MineYourCoins");
    save(out, "wifi-setup"); first = pixels(); testMillis += 3000;
    ui::showPortal("BDLMinerV1-very-long-device-label", "MineYourCoins");
    assert(pixels() != first);
    ui::showMessage("WiFi", "Connecting...", "Open local dashboard"); save(out, "message");
    ui::blockFound(UINT32_MAX); first = pixels(); ui::nextScreen();
    assert(pixels() == first); assert(ui::dismissAlert() && !ui::dismissAlert());
    std::cout << "Gamma layout: native 128x32/1-bit, four bounded rows, 0/180 rotation, font reset, large counters, full error/SSID paging, UTF-8, newline isolation, timer wrap and missing sensors passed\n";
}
