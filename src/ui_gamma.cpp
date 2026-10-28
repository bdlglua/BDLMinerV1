#include "ui.h"
#ifdef BOARD_GAMMA601
#include <WiFi.h>
#include "board.h"
#include "gamma601.h"
#include "miner.h"
#include "settings.h"
#include "core/work.h"
#include <cmath>
namespace ui {
namespace {
lgfx::LovyanGFX *oled = nullptr;
uint8_t page = 0, brightness = 100;
uint32_t since = 0, foundHeight = 0;
bool alertShown = false;
void lines(const String &a, const String &b, const String &c, const String &d) {
    if (!oled || !brightness) return;
    oled->startWrite(); oled->fillScreen(0); oled->setTextColor(0xffffff, 0);
    oled->setTextSize(1); oled->setTextWrap(false);
    const String *rows[]{&a, &b, &c, &d};
    for (int i = 0; i < 4; ++i) { oled->setCursor(0, i * 8); oled->print(rows[i]->substring(0,21)); }
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
void nextScreen() { if (alertShown) return; page = (page + 1) % 3; since = millis(); render(); }
void cycleBrightness() { settingsSetBrightness(settingsBrightness()?0:80); setBacklight(settingsBrightness()); }
void setBacklight(uint8_t value) {
    brightness = value;
    if (oled) { if (value) static_cast<lgfx::LGFX_Device *>(oled)->wakeup(); else static_cast<lgfx::LGFX_Device *>(oled)->sleep(); }
}
void setRotation(uint8_t rotation) { if (oled) oled->setRotation(rotation >= 2 ? 2 : 0); }
void flashShare() {} // Do not flash the OLED on each difficulty-1 ASIC result.
void blockFound(uint32_t height) { foundHeight = height; alertShown = true; render(); }
bool dismissAlert() { if (!alertShown) return false; alertShown = false; since = millis(); render(); return true; }
uint32_t screenAge() { return millis()-since; }
void render() {
    if (alertShown) { lines("*** BLOCK FOUND! ***",String("Height ")+foundHeight,"Check pool and wallet","Press the button"); return; }
    auto s = minerStats(); auto a = gamma601::status();
    String temp = std::isfinite(a.temperature)?String(a.temperature,1):"--";
    if (!a.running) { lines("Gamma 601 / BM1370",a.state,temp+" C  Fan "+String(a.rpm),WiFi.localIP().toString()); return; }
    if (page == 0) lines(a.stockFallback?"BM1370 525M FALLBACK":"BDLMiner / BM1370",String(work::formatSI(minerHashrateLive(),2).c_str())+"H/s",
        temp+" C  Fan "+String(a.rpm),String("Accepted ")+s.accepted+" / "+s.rejected);
    else if (page == 1) lines("Power / thermal",String(a.inputVoltage,2)+"V in  "+String(a.coreVoltage,3)+"V core",
        String(a.frequency,0)+" MHz  "+String(a.current,1)+"A",String("VR ")+String(a.regulatorTemperature,1)+" C");
    else lines("Pool / records",s.poolConnected?"Pool connected":"Pool disconnected",String("Best ")+work::formatSI(s.bestDiff,2).c_str(),WiFi.localIP().toString());
}
}
#endif
