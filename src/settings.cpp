#include "settings.h"

#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>

Settings settings;

static const char *kNamespace = "bdlminer";

void settingsLoad() {
    Preferences p;
    p.begin(kNamespace, true);
    settings.poolHost = p.getString("pool", DEFAULT_POOL_HOST);
    settings.poolPort = p.getUShort("port", DEFAULT_POOL_PORT);
    settings.backupHost = p.getString("pool2", "");
    settings.backupPort = p.getUShort("port2", DEFAULT_BACKUP_PORT);
    settings.btcAddress = p.getString("addr", "");
    settings.poolPassword = p.getString("pass", DEFAULT_POOL_PASSWORD);
    settings.suggestedDifficulty = p.getDouble("diff", DEFAULT_DIFFICULTY);
    settings.brightness = p.getUChar("bright", 80);
    settings.rotation = p.getUChar("rot", 1);
    settings.timezone = p.getString("tz", DEFAULT_TIMEZONE);
    settings.currency = p.getString("fiat", DEFAULT_CURRENCY);
    p.end();
}

void settingsSave() {
    Preferences p;
    p.begin(kNamespace, false);
    p.putString("pool", settings.poolHost);
    p.putUShort("port", settings.poolPort);
    p.putString("pool2", settings.backupHost);
    p.putUShort("port2", settings.backupPort);
    p.putString("addr", settings.btcAddress);
    p.putString("pass", settings.poolPassword);
    p.putDouble("diff", settings.suggestedDifficulty);
    p.putUChar("bright", settings.brightness);
    p.putUChar("rot", settings.rotation);
    p.putString("tz", settings.timezone);
    p.putString("fiat", settings.currency);
    p.end();
}

String portalApName() {
    uint64_t mac = ESP.getEfuseMac();
    char buf[24];
    snprintf(buf, sizeof(buf), "BDLMinerV1-%04X", (unsigned)((mac >> 32) & 0xFFFF));
    return buf;
}

static bool shouldSave = false;

// Currencies quoted by mempool.space/api/v1/prices.
static const char *const kCurrencies[] = {"USD", "EUR", "GBP", "CAD", "CHF", "AUD", "JPY"};

static String normalizeCurrency(String c) {
    c.trim();
    c.toUpperCase();
    for (const char *k : kCurrencies)
        if (c == k) return c;
    return DEFAULT_CURRENCY;
}

void wifiSetup(bool forcePortal, void (*onPortal)()) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    WiFiManager wm;
    wm.setDebugOutput(false);
    wm.setTitle("BDLMiner V1");
    wm.setConfigPortalTimeout(PORTAL_TIMEOUT_S);
    wm.setConnectTimeout(20);
    wm.setSaveConfigCallback([]() { shouldSave = true; });
    wm.setAPCallback([onPortal](WiFiManager *) {
        if (onPortal) onPortal();
    });

    char port[8], port2[8], diff[16];
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
    WiFiManagerParameter pTz("tz", "Timezone (POSIX TZ, e.g. CET-1CEST,M3.5.0,M10.5.0/3 or UTC0)",
                             settings.timezone.c_str(), 48);
    WiFiManagerParameter pFiat("fiat", "Price currency (USD, EUR, GBP, CAD, CHF, AUD, JPY)",
                               settings.currency.c_str(), 4);
    wm.addParameter(&pHost);
    wm.addParameter(&pPort);
    wm.addParameter(&pHost2);
    wm.addParameter(&pPort2);
    wm.addParameter(&pAddr);
    wm.addParameter(&pPass);
    wm.addParameter(&pDiff);
    wm.addParameter(&pTz);
    wm.addParameter(&pFiat);

    // Without a payout address there is nothing useful to mine for.
    if (settings.btcAddress.isEmpty()) forcePortal = true;

    String ap = portalApName();
    bool connected;
    if (forcePortal) {
        connected = wm.startConfigPortal(ap.c_str(), PORTAL_AP_PASSWORD);
    } else {
        connected = wm.autoConnect(ap.c_str(), PORTAL_AP_PASSWORD);
    }

    if (shouldSave) {
        settings.poolHost = pHost.getValue();
        settings.poolHost.trim();
        int p = atoi(pPort.getValue());
        if (p > 0 && p < 65536) settings.poolPort = p;
        settings.backupHost = pHost2.getValue();
        settings.backupHost.trim();
        p = atoi(pPort2.getValue());
        if (p > 0 && p < 65536) settings.backupPort = p;
        settings.btcAddress = pAddr.getValue();
        settings.btcAddress.trim();
        settings.poolPassword = pPass.getValue();
        double d = atof(pDiff.getValue());
        if (d > 0) settings.suggestedDifficulty = d;
        settings.timezone = pTz.getValue();
        settings.timezone.trim();
        if (settings.timezone.isEmpty()) settings.timezone = DEFAULT_TIMEZONE;
        settings.currency = normalizeCurrency(pFiat.getValue());
        settingsSave();
        Serial.println("[cfg] settings saved");
    }

    if (!connected || settings.btcAddress.isEmpty()) {
        Serial.println("[wifi] not configured, restarting");
        delay(1000);
        ESP.restart();
    }
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
