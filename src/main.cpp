// BDLMiner V1 - solo/lottery Bitcoin miner for the M5StickC Plus2 and other
// ESP32 boards. Connects to a Stratum V1 pool (public-pool.io by default).

#include <Arduino.h>
#include <WiFi.h>

#include "board.h"
#include "core/address.h"
#include "log.h"
#include "market.h"
#include "miner.h"
#include "mqtt.h"
#include "net.h"
#include "notify.h"
#include "settings.h"
#include "ui.h"
#include "watch.h"
#include "web.h"

static void onPortalStarted() {
    String ap = portalApName();
    logf("[wifi] setup portal: join \"%s\" (password %s), then open http://192.168.4.1\n", ap.c_str(),
         PORTAL_AP_PASSWORD);
    ui::showPortal(ap, PORTAL_AP_PASSWORD);
}

static void logStats() {
    // Per-engine average since the previous log line ("hw 970.1 + sw 25.3").
    static uint32_t lastHashes[MAX_MINERS], lastMs = 0;
    uint32_t now = millis();
    char engines[64] = "";
    if (lastMs) {
        size_t len = 0;
        for (int i = 0; i < stats.minerCount && len < sizeof(engines); i++)
            len += snprintf(engines + len, sizeof(engines) - len, "%s%s %.1f", i ? " + " : " (", stats.engine[i],
                            (uint32_t)(stats.hashes[i] - lastHashes[i]) / (double)(now - lastMs));
        if (len < sizeof(engines)) strlcat(engines, ")", sizeof(engines));
    }
    for (int i = 0; i < stats.minerCount; i++) lastHashes[i] = stats.hashes[i];
    lastMs = now;

    logf("[stats] %.2f kH/s%s | shares %u ok / %u rejected | best %.6g (ever %.6g) | blocks %u | %s\n",
         minerHashrate() / 1000.0, engines, (unsigned)stats.accepted, (unsigned)stats.rejected,
         stats.sessionBestDiff, stats.bestDiff, (unsigned)stats.blocksFound, netStatus());
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    logf("BDLMiner V1 %s on %s\n", FW_VERSION, BOARD_NAME);
    logBoot();

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
    logf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
    logf("[cfg] pool %s:%u user %s\n", settings.poolHost.c_str(), settings.poolPort,
         settings.btcAddress.c_str());
    if (!settings.backupHost.isEmpty())
        logf("[cfg] backup pool %s:%u\n", settings.backupHost.c_str(), settings.backupPort);

    // Some pools use account names, so a bad address is only a warning.
    address::Info addr = address::parse(settings.btcAddress.c_str());
    if (addr.valid()) {
        logf("[cfg] address type %s%s\n", address::typeName(addr.type),
             addr.network == address::Network::Main ? "" : " (testnet)");
    } else {
        logf("[cfg] WARNING: not a valid Bitcoin address, rewards may be lost. "
             "Hold the main button at boot to fix it.\n");
        ui::showMessage("Warning", "Invalid BTC address?", "Hold A at boot to fix");
        delay(5000);
    }

    minerLoadStats();
    minerStart();
    notifyStart();
    netStart();
    marketStart();
    webStart();
    mqttStart();
    ui::render();
}

void loop() {
    static uint32_t lastTick = 0, lastLog = 0;
    uint32_t now = millis();

    board::Input input = board::poll();
    if (input != board::Input::None) watchWake();
    switch (input) {
        case board::Input::Next:
            ui::nextScreen();
            break;
        case board::Input::Brightness:
            ui::cycleBrightness();
            ui::render();
            break;
        case board::Input::FactoryReset:
            ui::showMessage("Reset", "Erasing settings...", "Rebooting into setup");
            logf("[cfg] factory reset\n");
            minerSaveStats();  // statistics survive a factory reset
            delay(1500);
            settingsFactoryReset();
            break;
        default:
            break;
    }

    if (now - lastTick >= 1000) {
        lastTick = now;
        minerTick();
        watchTick();
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
