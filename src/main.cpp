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
    // Per-engine rates, the same 1-minute averages as the total ("hw 948.2 + sw 41.0").
    char engines[64] = "";
    size_t len = 0;
    for (int i = 0; i < stats.minerCount && len < sizeof(engines); i++)
        len += snprintf(engines + len, sizeof(engines) - len, "%s%s %.1f", i ? " + " : " (", stats.engine[i],
                        minerEngineRate(i) / 1000.0);
    if (len && len < sizeof(engines)) strlcat(engines, ")", sizeof(engines));

    logf("[stats] %.2f kH/s%s | shares %u ok / %u rejected | best %.6g (ever %.6g) | blocks %u | %s\n",
         minerHashrate() / 1000.0, engines, (unsigned)stats.accepted, (unsigned)stats.rejected,
         stats.sessionBestDiff, stats.bestDiff, (unsigned)stats.blocksFound, netStatus());
}

void setup() {
    // Above the tasks that make HTTPS requests (market 2, updates and
    // notifications 1): a TLS handshake computes for 1.5-2 s without
    // yielding, which froze the screen and the web server meanwhile. loop()
    // sleeps 20 ms per pass, so they still get the core.
    vTaskPrioritySet(nullptr, 3);
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
    static uint32_t lastTick = 0, lastLog = 0, lastPass = 0;
    uint32_t now = millis();
    // Screen and web server stand still while this does not run.
    if (lastPass && now - lastPass > 1000) logf("[loop] stalled %u ms\n", (unsigned)(now - lastPass));
    lastPass = now;

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
