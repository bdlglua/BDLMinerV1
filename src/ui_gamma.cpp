#include "ui.h"
#ifdef BOARD_GAMMA601
#include <WiFi.h>
#include "board.h"
#include "gamma601.h"
#include "miner.h"
#include "settings.h"
#include "core/work.h"
#include "core/display_config.h"
#include <cmath>
namespace ui {
namespace {
lgfx::LovyanGFX *oled = nullptr;
uint8_t page = 0, brightness = 100;
uint32_t since = 0, foundHeight = 0;
bool alertShown = false;
String longRows[4];
uint32_t rowStarted[4]{};
String singleLine(const String &text) {
    String out;
    out.reserve(text.length());
    for (unsigned i = 0; i < text.length(); ++i) {
        uint8_t c = text[i];
        out += c < 32 || c == 127 ? ' ' : char(c);
    }
    return out;
}
unsigned rowEnd(const String &text, unsigned start, int width) {
    unsigned end = start, space = start;
    while (end < text.length()) {
        unsigned next = end + 1;
        // Never split a UTF-8 character between two display windows.
        while (next < text.length() && (uint8_t(text[next]) & 0xc0) == 0x80) ++next;
        if (oled->textWidth(text.substring(start, next).c_str()) > width) break;
        if (text[end] == ' ') space = next;
        end = next;
    }
    if (end < text.length() && space > start) return space;
    return end > start ? end : start + 1;
}
String rowWindow(const String &text, int width, uint32_t age) {
    unsigned count = 0;
    for (unsigned start = 0; start < text.length(); ++count) start = rowEnd(text, start, width);
    if (!count) return "";
    unsigned part = (age / 3000) % count, start = 0;
    while (part) { start = rowEnd(text, start, width); --part; }
    return text.substring(start, rowEnd(text, start, width));
}
String counter(uint32_t count) {
    return count < 1000000 ? String(count) : String(work::formatSI(count, 2).c_str());
}
String reading(float value, unsigned decimals) { return std::isfinite(value) ? String(value, decimals) : String("--"); }
void lines(const String &a, const String &b, const String &c, const String &d, bool centered = false) {
    if (!oled || !brightness) return;
    oled->setTextColor(0xffffff, 0);
    // SSD1306 is 128x32: Font0 is exactly 6x8, four independent 8px rows.
    oled->setFont(&lgfx::fonts::Font0); oled->setTextSize(1); oled->setTextWrap(false);
    const String *rows[]{&a, &b, &c, &d};
    String visible[4];
    uint32_t now = millis();
    for (int i = 0; i < 4; ++i) {
        String text = singleLine(*rows[i]);
        int width = oled->width() - 2;
        if (oled->textWidth(text.c_str()) > width) {
            if (longRows[i] != text) { longRows[i] = text; rowStarted[i] = now; }
            text = rowWindow(text, width, uint32_t(now - rowStarted[i]));
        } else longRows[i] = "";
        visible[i] = text;
    }
    // Formatting/paging above does not hold the OLED/ASIC's shared I2C bus.
    oled->startWrite(); oled->fillScreen(0);
    for (int i = 0; i < 4; ++i) {
        const String &text = visible[i];
        int x = centered ? (oled->width() - oled->textWidth(text.c_str())) / 2 : 1;
        oled->setCursor(x, i * 8); oled->print(text.c_str());
    }
    oled->endWrite(); oled->display();
}
}
void begin() {
    oled = board::display();
    if (oled) { oled->setRotation(settings.rotation >= 2 ? 2 : 0); oled->setColorDepth(1); }
    since = millis();
}
void showSplash() { lines("BDLMiner " FW_VERSION,"Bitaxe Gamma 601","Native BM1370 driver","EXPERIMENTAL"); }
void showPortal(const String &ap, const char *password) { lines("WiFi setup: connect to",ap,String("Key: ")+password,"Open 192.168.4.1"); }
void showMessage(const char *title, const char *line1, const char *line2) { lines(title,line1?line1:"",line2?line2:"",""); }
void nextScreen() { if (alertShown) return; page = display_config::nextPage(page,3,settings.screenPages); since = millis(); render(); }
void cycleBrightness() { settingsSetBrightness(settingsBrightness()?0:80); setBacklight(settingsBrightness()); }
void setBacklight(uint8_t value) {
    brightness = value;
    if (oled) { if (value) static_cast<lgfx::LGFX_Device *>(oled)->wakeup(); else static_cast<lgfx::LGFX_Device *>(oled)->sleep(); }
}
void setRotation(uint8_t rotation) { if (oled) oled->setRotation(rotation >= 2 ? 2 : 0); }
void applySettings() { if(!(settings.screenPages&(1u<<page)))page=0;setRotation(settings.rotation); render(); }
void flashShare() {} // Do not flash the OLED on each difficulty-1 ASIC result.
void blockFound(uint32_t height) { foundHeight = height; alertShown = true; render(); }
bool dismissAlert() { if (!alertShown) return false; alertShown = false; since = millis(); render(); return true; }
uint32_t screenAge() { return millis()-since; }
void render() {
    if (alertShown) { lines("BLOCK CANDIDATE",String("Height ")+foundHeight,"Check pool / chain","Press the button",true); return; }
    auto s = minerStats(); auto a = gamma601::status();
    // The ASIC diode, or the board's sensor while the ASIC is off.
    float t = gamma601::temperature();
    String temp = std::isfinite(t)?String(t,1):"--";
    if (!a.running) { lines("Gamma 601 / BM1370",a.state,temp+" C  Fan "+String(a.rpm),WiFi.localIP().toString()); return; }
    if(page==0&&settings.customScreen){
        display_config::Snapshot data;data.hashrate=minerHashrateLive();data.best=s.sessionBestDiff;
        data.accepted=s.accepted;data.temperature=t;data.uptime=minerUptimeSeconds();data.connected=s.poolConnected;
        data.frequency=a.frequency;data.fanRpm=a.rpm;
        const uint8_t widgets[]={settings.screenWidget0,settings.screenWidget1,settings.screenWidget2,settings.screenWidget3};
        String rows[4];for(unsigned i=0;i<4;++i){char value[32];display_config::value(widgets[i],data,value,sizeof(value),settings.language);
            rows[i]=String(display_config::label(widgets[i],settings.language))+" "+value;}
        lines(rows[0],rows[1],rows[2],rows[3]);return;
    }
    if (page == 0) lines(a.stockFallback?"BM1370 525M FALLBACK":"BDLMiner / BM1370",String(work::formatSI(minerHashrateLive(),2).c_str())+"H/s",
        temp+" C  Fan "+String(a.rpm),String("A:")+counter(s.accepted)+" R:"+counter(s.rejected));
    else if (page == 1) lines("Power / thermal",String("Vin ")+reading(a.inputVoltage,2)+"V Vc "+reading(a.coreVoltage,3)+"V",
        reading(a.frequency,0)+" MHz  "+reading(a.current,1)+"A",String("VR ")+reading(a.regulatorTemperature,1)+" C");
    else lines("Pool / records",s.poolConnected?"Pool connected":"Pool disconnected",String("Best ")+work::formatSI(s.bestDiff,2).c_str(),WiFi.localIP().toString());
}
}
#endif
