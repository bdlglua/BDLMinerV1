#include "settings.h"

#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_wifi.h>
#include <freertos/semphr.h>
#include <atomic>

#include "log.h"

Settings settings;
static std::atomic<uint8_t> liveBrightness{80};
static SemaphoreHandle_t settingsSaveMutex = xSemaphoreCreateMutex();

static const char *kNamespace = "bdlminer";

#define S(key, type, field, group, label) {key, SettingType::type, &settings.field, 0, group, label}
#define N(key, field, group, label) {key, SettingType::Number, &settings.field, 8 * sizeof(settings.field), group, label}

const SettingInfo kSettings[] = {
    S("pool", Text, poolHost, "Pool", "Pool host"),
    N("port", poolPort, "Pool", "Pool port"),
    S("addr", Text, btcAddress, "Pool", "BTC address (optionally address.worker)"),
    S("pass", Text, poolPassword, "Pool", "Pool password"),
    S("diff", Decimal, suggestedDifficulty, "Pool", "Suggested share difficulty"),
    S("ua", Text, userAgent, "Pool", "User agent (pool.nerdminers.org needs NerdMinerV2)"),
    S("pool2", Text, backupHost, "Pool", "Backup pool host (empty = none)"),
    N("port2", backupPort, "Pool", "Backup pool port"),
    N("rotateh", rotateHours, "Pool", "Alternate main / backup pool every N hours (0 = only on failure)"),
    N("bright", brightness, "Display", "Brightness (%)"),
    N("rot", rotation, "Display", "Rotation (0-3)"),
    S("autorot", Bool, autoRotate, "Display", "Rotate with the accelerometer (M5 devices)"),
    N("lang", language, "Display", "Language: 0 English, 1 French"),
    N("theme", theme, "Display", "Colour theme (0 orange, 1 green, 2 blue, 3 purple, 4 pink)"),
    N("cycle", cycleSeconds, "Display", "Next screen every N seconds (0 = off)"),
    N("nightfrom", nightFrom, "Display", "Night mode from hour (0-23, 255 = off)"),
    N("nightto", nightTo, "Display", "Night mode until hour (0-23)"),
    S("beep", Bool, beep, "Display", "Beep on a 32-bit share"),
    S("tz", Text, timezone, "Display", "Timezone (POSIX TZ, e.g. CET-1CEST,M3.5.0,M10.5.0/3 or UTC0)"),
    S("fiat", Text, currency, "Display", "Price currency (USD, EUR, GBP, CAD, CHF, AUD, JPY)"),
    S("ntfy", Text, ntfyTopic, "Notifications", "ntfy topic (empty = off)"),
    S("ntfysrv", Text, ntfyServer, "Notifications", "ntfy server"),
    S("notify32", Bool, notify32, "Notifications", "Also notify every 32-bit share"),
    N("maxtemp", maxTemp, "Safety", "Pause mining above this temperature in C (0 = off)"),
    N("power", miningPower, "Safety", "Mining power on USB and battery (0-100%, 0 = paused)"),
    N("rebooth", rebootHours, "Safety", "Restart after N hours without an accepted share (0 = off)"),
    S("saver", Bool, batterySaver, "Safety", "Battery saver: dim and mine at half speed on battery"),
    S("wifi2", Text, wifi2Ssid, "Network", "Fallback WiFi 1 name"),
    S("wpass2", Secret, wifi2Pass, "Network", "Fallback WiFi 1 password"),
    S("wifi3", Text, wifi3Ssid, "Network", "Fallback WiFi 2 name"),
    S("wpass3", Secret, wifi3Pass, "Network", "Fallback WiFi 2 password"),
    S("mqtt", Text, mqttHost, "Network", "MQTT broker for Home Assistant (empty = off)"),
    N("mqttport", mqttPort, "Network", "MQTT port"),
    S("mqttuser", Text, mqttUser, "Network", "MQTT user"),
    S("mqttpass", Secret, mqttPass, "Network", "MQTT password"),
    S("peers", Text, peers, "Network", "Other miners on the dashboard (comma separated IPs or names)"),
    S("webpass", Secret, webPassword, "Network", "Web password (settings and firmware update from the dashboard)"),
    N("update", githubUpdate, "Network", "Updates from GitHub: 0 off, 1 check and offer, 2 install automatically"),
    S("count", Bool, usageCount, "Network",
      "Anonymous usage count: once a day, download a 2-byte file of this version's release (nothing is sent)"),
};
const size_t kSettingCount = sizeof(kSettings) / sizeof(kSettings[0]);

#undef S
#undef N

// Currencies quoted by mempool.space/api/v1/prices.
static const char *const kCurrencies[] = {"USD", "EUR", "GBP", "CAD", "CHF", "AUD", "JPY"};

static String normalizeCurrency(String c) {
    c.trim();
    c.toUpperCase();
    for (const char *k : kCurrencies)
        if (c == k) return c;
    return DEFAULT_CURRENCY;
}

// "stratum+tcp://pool.example.com:3333/" -> host "pool.example.com", port 3333
// (with `port`; without it a ":port" stays in `host`). Pool addresses are
// often pasted that way.
static void cleanHost(String &host, uint16_t *port) {
    host.trim();
    int scheme = host.indexOf("://");
    if (scheme >= 0) host.remove(0, scheme + 3);
    int slash = host.indexOf('/');
    if (slash >= 0) host.remove(slash);
    int colon = host.lastIndexOf(':');
    if (port && colon > 0 && host.indexOf(':') == colon) {  // not an IPv6 address
        long p = host.substring(colon + 1).toInt();
        if (port && p > 0 && p < 65536) *port = p;
        host.remove(colon);
    }
}

void settingsSanitize(Settings &settings) {
    for (size_t i = 0; i < kSettingCount; i++)
        if (kSettings[i].type == SettingType::Text) ((String *)settingValue(kSettings[i], settings))->trim();
    cleanHost(settings.poolHost, &settings.poolPort);
    cleanHost(settings.backupHost, &settings.backupPort);
    cleanHost(settings.ntfyServer, nullptr);
    cleanHost(settings.mqttHost, &settings.mqttPort);
    if (!settings.poolPort) settings.poolPort = DEFAULT_POOL_PORT;
    if (!settings.backupPort) settings.backupPort = DEFAULT_BACKUP_PORT;
    if (!(settings.suggestedDifficulty > 0)) settings.suggestedDifficulty = DEFAULT_DIFFICULTY;
    // "BDLMinerV1/x.y.z" was the default before 1.0.4: saved settings still
    // carry it, and pool.nerdminers.org refuses it.
    if (settings.userAgent.isEmpty() || settings.userAgent.startsWith("BDLMinerV1/"))
        settings.userAgent = DEFAULT_USER_AGENT;
    if (settings.timezone.isEmpty()) settings.timezone = DEFAULT_TIMEZONE;
    if (settings.ntfyServer.isEmpty()) settings.ntfyServer = DEFAULT_NTFY_SERVER;
    settings.currency = normalizeCurrency(settings.currency);
    if (settings.brightness > 100) settings.brightness = 100;
    settings.rotation &= 3;
    if (settings.language > 1) settings.language = 0;
    if (settings.githubUpdate > 2) settings.githubUpdate = 2;
    if (settings.maxTemp && settings.maxTemp < 30) settings.maxTemp = 30;  // resumes 5 C below
    if (settings.miningPower > 100) settings.miningPower = 100;
    if (settings.nightFrom > 23) settings.nightFrom = HOUR_OFF;
    if (settings.nightTo > 23) settings.nightTo = HOUR_OFF;
}

void settingsLoad() {
    Preferences p;
    p.begin(kNamespace, true);
    for (size_t i = 0; i < kSettingCount; i++) {
        const SettingInfo &s = kSettings[i];
        switch (s.type) {
            case SettingType::Text:
            case SettingType::Secret: {
                String *v = (String *)s.value;
                *v = p.getString(s.key, *v);
                break;
            }
            case SettingType::Number:
                if (s.bits == 8) *(uint8_t *)s.value = p.getUChar(s.key, *(uint8_t *)s.value);
                else *(uint16_t *)s.value = p.getUShort(s.key, *(uint16_t *)s.value);
                break;
            case SettingType::Decimal: *(double *)s.value = p.getDouble(s.key, *(double *)s.value); break;
            case SettingType::Bool: *(bool *)s.value = p.getBool(s.key, *(bool *)s.value); break;
        }
    }
    // From 1.0.7 every miner installs updates by itself: settings saved before
    // (every save stored the value) switch to 2; those saved since carry
    // "updv" and keep their choice.
    if (!p.isKey("updv")) settings.githubUpdate = 2;
    p.end();
    settingsSanitize();
    liveBrightness = settings.brightness;
}

uint8_t settingsBrightness() { return liveBrightness.load(); }

void settingsSetBrightness(uint8_t percent) {
    liveBrightness = percent > 100 ? 100 : percent;
    Settings copy = settings;
    copy.brightness = liveBrightness;
    settingsSave(copy);
}

void settingsSave(const Settings &values, bool forRestart) {
    xSemaphoreTake(settingsSaveMutex, portMAX_DELAY);
    static bool stagedForRestart = false;
    // A brightness button during the reboot delay must not overwrite a
    // staged web/backup configuration with the old live settings.
    if (stagedForRestart && !forRestart) {
        xSemaphoreGive(settingsSaveMutex);
        return;
    }
    Preferences p;
    p.begin(kNamespace, false);
    for (size_t i = 0; i < kSettingCount; i++) {
        const SettingInfo &s = kSettings[i];
        const void *value = settingValue(s, values);
        switch (s.type) {
            case SettingType::Text:
            case SettingType::Secret: p.putString(s.key, *(const String *)value); break;
            case SettingType::Number:
                if (s.bits == 8) p.putUChar(s.key, *(const uint8_t *)value);
                else p.putUShort(s.key, *(const uint16_t *)value);
                break;
            case SettingType::Decimal: p.putDouble(s.key, *(const double *)value); break;
            case SettingType::Bool: p.putBool(s.key, *(const bool *)value); break;
        }
    }
    p.putUChar("updv", 1);  // see settingsLoad()
    p.end();
    if (forRestart) stagedForRestart = true;
    xSemaphoreGive(settingsSaveMutex);
}

String portalApName() {
    uint64_t mac = ESP.getEfuseMac();
    char buf[24];
    snprintf(buf, sizeof(buf), "BDLMinerV1-%04X", (unsigned)((mac >> 32) & 0xFFFF));
    return buf;
}

static bool shouldSave = false;

// The portal's network, read once connected: fallbacks are joined without
// saving them, so this is how to get back to it.
static String mainSsid, mainPass;

static bool waitConnected(uint32_t ms) {
    for (uint32_t t0 = millis(); millis() - t0 < ms;) {
        if (WiFi.isConnected()) return true;
        delay(100);
    }
    return WiFi.isConnected();
}

bool wifiTryFallbacks() {
    const String *nets[][2] = {{&mainSsid, &mainPass},
                               {&settings.wifi2Ssid, &settings.wifi2Pass},
                               {&settings.wifi3Ssid, &settings.wifi3Pass}};
    WiFi.persistent(false);  // never replace the portal's network in flash
    bool ok = false;
    for (auto &n : nets) {
        if (n[0]->isEmpty()) continue;
        logf("[wifi] trying \"%s\"\n", n[0]->c_str());
        WiFi.disconnect();
        WiFi.begin(n[0]->c_str(), n[1]->c_str());
        if ((ok = waitConnected(15000))) break;
    }
    WiFi.persistent(true);
    return ok;
}

void wifiSetup(bool forcePortal, void (*onPortal)()) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    // Without a payout address there is nothing useful to mine for.
    if (settings.btcAddress.isEmpty()) forcePortal = true;

    // The network saved by the portal, also when a fallback ends up joined.
    wifi_config_t saved;
    if (esp_wifi_get_config(WIFI_IF_STA, &saved) == ESP_OK && saved.sta.ssid[0]) {
        char ssid[33] = {0}, pass[65] = {0};
        memcpy(ssid, saved.sta.ssid, sizeof(saved.sta.ssid));
        memcpy(pass, saved.sta.password, sizeof(saved.sta.password));
        mainSsid = ssid;
        mainPass = pass;
    }

    if (!forcePortal) {
        WiFi.begin();  // the network saved by the portal
        if (waitConnected(15000) || wifiTryFallbacks()) return;
    }

    WiFiManager wm;
    wm.setDebugOutput(false);
    wm.setTitle("BDLMiner V1");
    wm.setConfigPortalTimeout(PORTAL_TIMEOUT_S);
    wm.setConnectTimeout(20);
    wm.setSaveConfigCallback([]() { shouldSave = true; });
    wm.setAPCallback([onPortal](WiFiManager *) {
        if (onPortal) onPortal();
    });

    char port[8], port2[8], diff[16], beep[2] = {settings.beep ? '1' : '0', 0}, lang[2] = {char('0' + settings.language), 0};
    snprintf(port, sizeof(port), "%u", settings.poolPort);
    snprintf(port2, sizeof(port2), "%u", settings.backupPort);
    snprintf(diff, sizeof(diff), "%g", settings.suggestedDifficulty);

    WiFiManagerParameter pHost("pool", "Pool host", settings.poolHost.c_str(), 80);
    WiFiManagerParameter pPort("port", "Pool port", port, 6, "type='number' min='1' max='65535'");
    WiFiManagerParameter pHost2("pool2", "Backup pool host (optional, e.g. solo.ckpool.org)",
                                settings.backupHost.c_str(), 80);
    WiFiManagerParameter pPort2("port2", "Backup pool port", port2, 6, "type='number' min='1' max='65535'");
    WiFiManagerParameter pAddr("addr", "Your BTC address (optionally address.worker)",
                               settings.btcAddress.c_str(), 100);
    WiFiManagerParameter pPass("pass", "Pool password", settings.poolPassword.c_str(), 40);
    WiFiManagerParameter pDiff("diff", "Suggested share difficulty", diff, 15);
    WiFiManagerParameter pBeep("beep", "Beep on a 32-bit share (1 = on, 0 = off)", beep, 1,
                               "type='number' min='0' max='1'");
    WiFiManagerParameter pUa("ua", "User agent (pool.nerdminers.org needs NerdMinerV2)", settings.userAgent.c_str(), 40);
    WiFiManagerParameter pTz("tz", "Timezone (POSIX TZ, e.g. CET-1CEST,M3.5.0,M10.5.0/3 or UTC0)",
                             settings.timezone.c_str(), 48);
    WiFiManagerParameter pFiat("fiat", "Price currency (USD, EUR, GBP, CAD, CHF, AUD, JPY)",
                               settings.currency.c_str(), 4);
    WiFiManagerParameter pLang("lang", "Language: 0 English, 1 French (Francais)", lang, 1,
                               "type='number' min='0' max='1'");
    WiFiManagerParameter pWeb("webpass",
                              "Web password: lets the dashboard change settings and update the firmware "
                              "(empty = read-only dashboard)",
                              settings.webPassword.c_str(), 40, "type='password'");
    wm.addParameter(&pHost);
    wm.addParameter(&pPort);
    wm.addParameter(&pHost2);
    wm.addParameter(&pPort2);
    wm.addParameter(&pAddr);
    wm.addParameter(&pPass);
    wm.addParameter(&pDiff);
    wm.addParameter(&pUa);
    wm.addParameter(&pBeep);
    wm.addParameter(&pTz);
    wm.addParameter(&pFiat);
    wm.addParameter(&pLang);
    wm.addParameter(&pWeb);

    String ap = portalApName();
    bool connected;
    if (forcePortal) {
        connected = wm.startConfigPortal(ap.c_str(), PORTAL_AP_PASSWORD);
    } else {
        connected = wm.autoConnect(ap.c_str(), PORTAL_AP_PASSWORD);
    }

    if (shouldSave) {
        settings.poolHost = pHost.getValue();
        settings.poolPort = atoi(pPort.getValue());
        settings.backupHost = pHost2.getValue();
        settings.backupPort = atoi(pPort2.getValue());
        settings.btcAddress = pAddr.getValue();
        settings.poolPassword = pPass.getValue();
        settings.suggestedDifficulty = atof(pDiff.getValue());
        settings.userAgent = pUa.getValue();
        settings.beep = atoi(pBeep.getValue()) != 0;
        settings.timezone = pTz.getValue();
        settings.currency = pFiat.getValue();
        settings.webPassword = pWeb.getValue();
        settings.language = atoi(pLang.getValue());
        settingsSanitize();
        settingsSave();
        logf("[cfg] settings saved\n");
    }

    if (!connected || settings.btcAddress.isEmpty()) {
        logf("[wifi] not configured, restarting\n");
        delay(1000);
        ESP.restart();
    }
    mainSsid = WiFi.SSID();
    mainPass = WiFi.psk();
}

void settingsFactoryReset() {
    Preferences p;
    p.begin(kNamespace, false);
    p.clear();
    p.end();
    WiFi.disconnect(true, true);  // also erases stored credentials
    delay(500);
    ESP.restart();
}
