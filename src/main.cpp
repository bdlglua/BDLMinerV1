// BDLMiner V1 - solo/lottery Bitcoin miner for the M5StickC Plus2 and other
// ESP32 boards. Connects to a Stratum V1 pool (public-pool.io by default).

#include <Arduino.h>
#include <WiFi.h>

#include "board.h"
#include "core/address.h"
#include "market.h"
#include "miner.h"
#include "net.h"
#include "settings.h"
#include "ui.h"
#include "web.h"

static void onPortalStarted() {
    String ap = portalApName();
    Serial.printf("[wifi] setup portal: join \"%s\" (password %s), then open http://192.168.4.1\n", ap.c_str(),
                  PORTAL_AP_PASSWORD);
    ui::showPortal(ap, PORTAL_AP_PASSWORD);
}

static void logStats() {
    Serial.printf("[stats] %.2f kH/s | shares %u ok / %u rejected | best %.6g (ever %.6g) | blocks %u | %s\n",
                  minerHashrate() / 1000.0, (unsigned)stats.accepted, (unsigned)stats.rejected, stats.sessionBestDiff,
                  stats.bestDiff, (unsigned)stats.blocksFound, netStatus());
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\nBDLMiner V1 %s on %s\n", FW_VERSION, BOARD_NAME);

    board::begin();
    settingsLoad();
    ui::begin();
    ui::showSplash();

    // Holding the main button during the first 2 s opens the setup portal.
    bool forcePortal = false;
    for (uint32_t t0 = millis(); millis() - t0 < 2000;) {
        if (board::mainButtonHeld()) forcePortal = true;
        delay(20);
    }

    ui::showMessage("WiFi", "Connecting...");
    wifiSetup(forcePortal, onPortalStarted);
    Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[cfg] pool %s:%u user %s\n", settings.poolHost.c_str(), settings.poolPort,
                  settings.btcAddress.c_str());
    if (!settings.backupHost.isEmpty())
        Serial.printf("[cfg] backup pool %s:%u\n", settings.backupHost.c_str(), settings.backupPort);

    // Some pools use account names, so a bad address is only a warning.
    address::Info addr = address::parse(settings.btcAddress.c_str());
    if (addr.valid()) {
        Serial.printf("[cfg] address type %s%s\n", address::typeName(addr.type),
                      addr.network == address::Network::Main ? "" : " (testnet)");
    } else {
        Serial.println("[cfg] WARNING: not a valid Bitcoin address, rewards may be lost. "
                       "Hold the main button at boot to fix it.");
        ui::showMessage("Warning", "Invalid BTC address?", "Hold A at boot to fix");
        delay(5000);
    }

    minerLoadStats();
    minerStart();
    netStart();
    marketStart();
    webStart();
    ui::render();
}

void loop() {
    static uint32_t lastTick = 0, lastLog = 0, lastOver32 = 0;
    uint32_t now = millis();

    // A hash with 32 leading zero bits (difficulty >= 1), counted by the miners.
    uint32_t over32 = stats.over32bits;
    if (over32 != lastOver32) {
        lastOver32 = over32;
        Serial.printf("[miner] 32-bit share found (%u so far)\n", (unsigned)over32);
        if (settings.beep) board::beep();
    }

    switch (board::poll()) {
        case board::Input::Next:
            ui::nextScreen();
            break;
        case board::Input::Brightness:
            ui::cycleBrightness();
            ui::render();
            break;
        case board::Input::FactoryReset:
            ui::showMessage("Reset", "Erasing settings...", "Rebooting into setup");
            Serial.println("[cfg] factory reset");
            delay(1500);
            settingsFactoryReset();
            break;
        default:
            break;
    }

    if (now - lastTick >= 1000) {
        lastTick = now;
        minerTick();
        ui::render();
        // Headless status LED: steady while connecting, blinking while mining.
        board::setLed(stats.poolConnected ? (now / 1000) % 2 : true);
    }
    if (now - lastLog >= 30000) {
        lastLog = now;
        logStats();
    }
    webHandle();
    delay(20);
}
