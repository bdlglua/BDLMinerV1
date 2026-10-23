// Persistent user settings (NVS), the WiFiManager configuration portal and
// the table that describes every setting to the web page (export / import).
#pragma once

#include <Arduino.h>

#define DEFAULT_POOL_HOST "public-pool.io"
#define DEFAULT_POOL_PORT 21496
#define DEFAULT_POOL_PASSWORD "x"
#define DEFAULT_BACKUP_PORT 3333
#define DEFAULT_DIFFICULTY 0.0002
#define PORTAL_AP_PASSWORD "MineYourCoins"
#define PORTAL_TIMEOUT_S 180
// POSIX TZ string (Central European Time with daylight saving by default).
#define DEFAULT_TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"
#define DEFAULT_CURRENCY "USD"
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif
// Sent in mining.subscribe. Some pools only accept known miners (e.g.
// pool.nerdminers.org requires "NerdMinerV2").
#define DEFAULT_USER_AGENT "BDLMinerV1/" FW_VERSION
#define DEFAULT_NTFY_SERVER "ntfy.sh"
// Firmware updates come from the releases of this GitHub repository.
#ifndef GITHUB_REPO
#define GITHUB_REPO "bdlglua/BDLMinerV1"
#endif
#define HOUR_OFF 255  // nightFrom / nightTo: no night mode

struct Settings {
    // Pool
    String poolHost = DEFAULT_POOL_HOST;
    uint16_t poolPort = DEFAULT_POOL_PORT;
    String backupHost;  // used when the main pool keeps failing, empty = none
    uint16_t backupPort = DEFAULT_BACKUP_PORT;
    uint8_t rotateHours = 0;  // > 0: alternate main and backup pool every N hours
    String btcAddress;  // payout address (worker name may be appended: addr.worker)
    String poolPassword = DEFAULT_POOL_PASSWORD;
    double suggestedDifficulty = DEFAULT_DIFFICULTY;
    String userAgent = DEFAULT_USER_AGENT;
    // Display and sound
    uint8_t brightness = 80;  // percent
    uint8_t rotation = 1;     // 0..3, display orientation
    bool autoRotate = false;  // follow the accelerometer (M5 devices with an IMU)
    uint8_t theme = 0;        // accent colour, see ui.cpp
    uint8_t language = 0;     // 0 English, 1 French (screen and dashboard)
    uint8_t cycleSeconds = 0; // > 0: next screen every N seconds
    uint8_t nightFrom = HOUR_OFF, nightTo = HOUR_OFF;  // screen off and no beep
    bool beep = true;  // beep when a 32-bit share (difficulty >= 1) is found
    String timezone = DEFAULT_TIMEZONE;
    String currency = DEFAULT_CURRENCY;  // fiat for the BTC price (USD, EUR...)
    // Notifications (ntfy.sh or a self-hosted ntfy server)
    String ntfyTopic;  // empty = off
    String ntfyServer = DEFAULT_NTFY_SERVER;
    bool notify32 = false;  // also every 32-bit share
    // Safety
    uint8_t maxTemp = 0;     // > 0: pause mining above this temperature (C)
    uint8_t rebootHours = 0; // > 0: restart after N hours without an accepted share
    bool batterySaver = false;  // on battery: dim the screen, mine at half speed
    // Network
    String wifi2Ssid, wifi2Pass, wifi3Ssid, wifi3Pass;  // fallbacks to the portal's network
    String mqttHost;  // empty = no MQTT / Home Assistant
    uint16_t mqttPort = 1883;
    String mqttUser, mqttPass;
    String peers;  // other miners for the dashboard: "192.168.1.41, miner2.local"
    String webPassword;  // empty = settings and firmware cannot be changed from the web
    uint8_t githubUpdate = 1;  // 0 off, 1 check and offer, 2 install by itself
};

extern Settings settings;

void settingsLoad();
void settingsSave();
// Trims texts and brings out-of-range values back to defaults.
void settingsSanitize();

// Every setting the web page can show and change (also the export format).
enum class SettingType { Text, Secret, Number, Decimal, Bool };
struct SettingInfo {
    const char *key;    // NVS key and JSON name
    SettingType type;
    void *value;        // String, uint8_t / uint16_t (by `bits`), double or bool
    uint8_t bits;       // 8 or 16 for Number
    const char *group;
    const char *label;
};
extern const SettingInfo kSettings[];
extern const size_t kSettingCount;

// Name of the access point opened by the configuration portal.
String portalApName();

// Connects to WiFi using stored credentials, then the fallback networks.
// Opens the configuration portal when nothing is stored, when `forcePortal`
// is set, or when no network can be joined. `onPortal` is called right before
// the portal starts (to update the screen). Restarts the board on timeout.
void wifiSetup(bool forcePortal, void (*onPortal)());

// Tries the fallback networks (settings.wifi2/wifi3) once; true if connected.
bool wifiTryFallbacks();

// Erase WiFi credentials and settings, then reboot into the portal.
void settingsFactoryReset();
