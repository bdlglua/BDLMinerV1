// Persistent user settings (NVS) and the WiFiManager configuration portal.
#pragma once

#include <Arduino.h>

#define DEFAULT_POOL_HOST "public-pool.io"
#define DEFAULT_POOL_PORT 21496
#define DEFAULT_POOL_PASSWORD "x"
#define DEFAULT_DIFFICULTY 0.0002
#define PORTAL_AP_PASSWORD "MineYourCoins"
#define PORTAL_TIMEOUT_S 180
// POSIX TZ string (Central European Time with daylight saving by default).
#define DEFAULT_TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"
#define DEFAULT_CURRENCY "USD"

struct Settings {
    String poolHost = DEFAULT_POOL_HOST;
    uint16_t poolPort = DEFAULT_POOL_PORT;
    String btcAddress;  // payout address (worker name may be appended: addr.worker)
    String poolPassword = DEFAULT_POOL_PASSWORD;
    double suggestedDifficulty = DEFAULT_DIFFICULTY;
    uint8_t brightness = 80;  // percent
    uint8_t rotation = 1;     // 0..3, display orientation
    String timezone = DEFAULT_TIMEZONE;
    String currency = DEFAULT_CURRENCY;  // fiat for the BTC price (USD, EUR...)
};

extern Settings settings;

void settingsLoad();
void settingsSave();

// Name of the access point opened by the configuration portal.
String portalApName();

// Connects to WiFi using stored credentials. Opens the configuration portal
// when nothing is stored, when `forcePortal` is set, or when the connection
// fails. `onPortal` is called right before the portal starts (to update the
// screen). Restarts the board on timeout.
void wifiSetup(bool forcePortal, void (*onPortal)());

// Erase WiFi credentials and settings, then reboot into the portal.
void settingsFactoryReset();
