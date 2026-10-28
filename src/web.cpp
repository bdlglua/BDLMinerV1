#include "web.h"
#ifdef BOARD_GAMMA601
#include "gamma601.h"
#endif

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

#include <algorithm>

#include "board.h"
#include "core/address.h"
#include "ghupdate.h"
#include "log.h"
#include "market.h"
#include "miner.h"
#include "net.h"
#include "ota.h"
#include "settings.h"
#include "settings_apply.h"
#include "watch.h"
#include "runtime.h"
#include "tls.h"
#include "web_validation.h"

#ifndef FW_ENV
#define FW_ENV "dev"
#endif

namespace {

WebServer server(80);
String hostName;
std::atomic<uint32_t> restartAt{0};  // restart due at this millis(), 0 = none
std::atomic<const char *> restartWhy{""};
HistoryPoint historyScratch[HISTORY_LEN]; // one HTTP worker; shared between its history handlers
uint32_t bootSession = 0;

// Generated from the maintainable web/ sources before every firmware build.
#include "web_assets.generated.h"

void sendPage(const char *title, const char *body, const char *script) {
    // Send one complete head and three fixed-length body segments instead of
    // framing every small tag as a separate HTTP chunk/TCP write.
    String head;
    head.reserve(768);
    head = "<!doctype html><html lang=\"";
    head += settings.language == 1 ? "fr" : "en";
    head += "\"><head><title>"; head += title; head += "</title>";
    head += "<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><meta name=\"color-scheme\" content=\"dark\">";
    head += "<link rel=\"stylesheet\" href=\"/assets/style.css?v="; head += kWebVersion; head += "\">";
    head += "<script>const LANG='"; head += settings.language == 1 ? "fr" : "en"; head += "';</script>";
    head += "<script defer src=\"/assets/common.js?v="; head += kWebVersion; head += "\"></script>";
    head += "<script defer src=\"/assets/"; head += script; head += ".js?v="; head += kWebVersion; head += "\"></script></head><body>";
    static const char end[] = "</div></div></body></html>";
    server.sendHeader("Cache-Control", "no-store");
    server.setContentLength(head.length() + strlen_P(kHeader) + strlen_P(body) + sizeof(end) - 1);
    server.send(200, "text/html; charset=utf-8", head);
    server.sendContent_P(kHeader);
    server.sendContent_P(body);
    server.sendContent(end);
}

void sendAsset(const char *mime, const uint8_t *data, size_t size) {
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "public, max-age=31536000, immutable");
    server.send_P(200, mime, reinterpret_cast<PGM_P>(data), size);
}

// Settings and firmware need the web password (set in the setup portal).
bool authorized() {
    if (settings.webPassword.isEmpty()) {
        server.send(403, "text/plain",
                    "Set a web password in the setup portal first (hold the main button while the miner starts).");
        return false;
    }
    if (!server.authenticate("admin", settings.webPassword.c_str())) {
        server.requestAuthentication(BASIC_AUTH, "BDLMiner", "Wrong password");
        return false;
    }
    return true;
}

void restartSoon(const char *why) {
    restartWhy = why;
    uint32_t due = millis() + 1500;
    restartAt = due ? due : 1; // zero is reserved for "no restart", including at millis() wrap
}

const char *accentCss() {
    static const char *kAccent[] = {"#f7931a", "#38d07a", "#3b9cf6", "#a66bf5", "#ec5aa0"};
    return kAccent[settings.theme % 5];
}

void handleStats() {
    const MinerStats stats = minerStats();
    JsonDocument j;
    j["board"] = board::modelName();
    j["firmware"] = FW_VERSION;
    j["boot_id"] = String(bootSession, HEX);
    j["uptime_s"] = minerUptimeSeconds();
    j["hashrate"] = minerHashrate();
    j["hashrate_live"] = minerHashrateLive();
    j["firmware_env"] = FW_ENV;
    j["total_hashes"] = (double)minerTotalHashes();
    JsonArray engines = j["engines"].to<JsonArray>();
    for (int i = 0; i < stats.minerCount; i++) {
        JsonObject e = engines.add<JsonObject>();
        e["name"] = stats.engine[i];
        e["rate"] = minerEngineRate(i);
    }
    j["shares"]["accepted"] = stats.accepted;
    j["shares"]["rejected"] = stats.rejected;
    j["shares"]["work"] = stats.acceptedWork;
    j["best"]["session"] = stats.sessionBestDiff;
    j["best"]["ever"] = stats.bestDiff;
    j["blocks"] = stats.blocksFound;
    j["over32bits"] = stats.over32bits;
    j["over64bits"] = stats.over64bits;
    BestShare top[TOP_SHARES];
    int nt = minerTopShares(top, TOP_SHARES);
    JsonArray ta = j["top"].to<JsonArray>();
    for (int i = 0; i < nt; i++) {
        JsonObject o = ta.add<JsonObject>();
        o["d"] = top[i].difficulty;
        o["t"] = top[i].time;
    }
    j["pool"]["host"] = netPoolHost();
    j["pool"]["port"] = netPoolPort();
    j["pool"]["backup"] = netOnBackup();
    j["pool"]["connected"] = stats.poolConnected;
    j["pool"]["status"] = netStatus();
    j["pool"]["difficulty"] = stats.poolDiff;
    j["pool"]["pending_shares"] = netPendingShares();
    j["pool"]["user_agent"] = netUserAgent();
    j["pool"]["protocol"] = netProtocol();
    j["pool"]["user_agent_auto"] = settings.userAgentAuto;
    PoolStats pv = poolStatsGet();
    j["pool_view"]["updated_s"] = pv.updatedMs ? (long)((millis() - pv.updatedMs) / 1000) : -1;
    j["pool_view"]["supported"] = pv.supported;
    j["pool_view"]["hashrate"] = pv.hashrate;
    j["pool_view"]["best"] = pv.bestEver;
    j["pool_view"]["shares"] = pv.shares;
    j["pool_view"]["workers"] = pv.workers;
    j["network"]["height"] = stats.blockHeight;
    j["network"]["difficulty"] = stats.networkDiff;
    j["address"] = settings.btcAddress;
    j["address_type"] = address::typeName(address::parse(settings.btcAddress.c_str()).type);
    j["lifetime"]["hashes"] = (double)stats.lifetimeHashes;
    j["lifetime"]["shares"] = stats.lifetimeShares;
    j["lifetime"]["blocks"] = stats.lifetimeBlocks;
    j["lifetime"]["seconds"] = stats.lifetimeSeconds;
    j["history_revision"] = minerHistoryRevision();
    if (server.arg("history") != "0") {
        int n = minerHistoryPoints(historyScratch, HISTORY_LEN);
        j["history"]["interval_s"] = HISTORY_INTERVAL_S;
        JsonArray hr = j["history"]["hashrate"].to<JsonArray>();
        for (int i = 0; i < n; i++) hr.add(double(historyScratch[i].hashrate));
    }
    j["heap"] = ESP.getFreeHeap();
    j["task_failures"] = runtime::failures().load();
    j["heap_max_block"] = ESP.getMaxAllocHeap();
    float temp = board::temperature();
    j["temperature_source"] = board::temperatureSource();
#ifdef BOARD_GAMMA601
    auto asic = gamma601::status();
    JsonObject a = j["asic"].to<JsonObject>();
    a["model"] = "BM1370"; a["experimental"] = true; a["running"] = asic.running; a["state"] = asic.state;
    a["frequency_mhz"] = asic.frequency; a["fan_rpm"] = asic.rpm; a["fan_percent"] = 100;
    a["input_v"] = asic.inputVoltage; a["core_v"] = asic.coreVoltage;
    a["current_a"] = asic.current; a["regulator_temp_c"] = asic.regulatorTemperature;
    a["regulator_status"] = asic.regulatorStatus;
    a["profile_mhz"] = asic.requestedMHz; a["stock_fallback"] = asic.stockFallback;
    a["fallback_reason"] = asic.fallbackReason;
    if (asic.hardwareErrorPercent < 0) a["hardware_error_percent"] = nullptr;
    else a["hardware_error_percent"] = asic.hardwareErrorPercent;
#endif
    if (isnan(temp)) j["temp_c"] = nullptr;
    else j["temp_c"] = (int)lroundf(temp);
    j["throttle"] = minerThrottle() == Throttle::Paused ? "paused" : minerThrottle() == Throttle::Half ? "half" : "full";
    j["power_percent"] = minerPowerPercent();
    j["power_limit_percent"] = settings.miningPower;
    j["max_temp_c"] = settings.maxTemp;
    j["limitation_reason"] = watchLimitReason();
    if (minerThrottle() == Throttle::Reduced) j["throttle"] = "reduced";
    j["night"] = watchNight();
    j["boot_reason"] = bootReason();
    j["accent"] = accentCss();
    j["ip"] = WiFi.localIP().toString();
    j["ssid"] = WiFi.SSID();
    j["rssi"] = WiFi.RSSI();
    JsonArray peers = j["peers"].to<JsonArray>();
    for (int from = 0; from < (int)settings.peers.length();) {
        int comma = settings.peers.indexOf(',', from);
        if (comma < 0) comma = settings.peers.length();
        String p = settings.peers.substring(from, comma);
        p.trim();
        if (p.length()) peers.add(p);
        from = comma + 1;
    }

    MarketData m = marketGet();
    JsonObject mk = j["market"].to<JsonObject>();
    mk["updated_s"] = m.updatedMs ? (long)((millis() - m.updatedMs) / 1000) : -1;
    mk["currency"] = settings.currency;
    mk["price"] = m.price;
    JsonArray fees = mk["fees"].to<JsonArray>();
    fees.add(m.feeFast);
    fees.add(m.feeHalfHour);
    fees.add(m.feeHour);
    mk["hashrate"] = m.networkHashrate;
    mk["mempool_tx"] = m.mempoolTx;
    mk["retarget"]["change"] = m.retargetChange;
    mk["retarget"]["progress"] = m.retargetProgress;
    mk["retarget"]["blocks"] = m.retargetBlocks;
    // Latest blocks, with your best share while each was the one to find
    // (the first entry is the block being mined, not found yet).
    BlockBest bb[BLOCK_BESTS];
    int nb = minerBlockBests(bb, BLOCK_BESTS);
    auto bestFor = [&](uint32_t h, JsonObject o) {
        for (int i = 0; i < nb; i++)
            if (bb[i].height == h) o["best"] = bb[i].best;
    };
    JsonArray blocks = j["blocks_recent"].to<JsonArray>();
    if (stats.blockHeight) {
        JsonObject o = blocks.add<JsonObject>();
        o["h"] = stats.blockHeight;
        o["mining"] = true;
        bestFor(stats.blockHeight, o);
    }
    for (int i = 0; i < m.blockCount; i++) {
        JsonObject o = blocks.add<JsonObject>();
        o["h"] = m.blocks[i].height;
        o["t"] = m.blocks[i].time;
        o["pool"] = m.blocks[i].pool;
        o["tx"] = m.blocks[i].txs;
        bestFor(m.blocks[i].height, o);
    }
    j["lang"] = settings.language == 1 ? "fr" : "en";
    GithubUpdate gu = ghUpdateState();
    JsonObject up = j["github_update"].to<JsonObject>();
    up["available"] = gu.available;
    up["mode"] = settings.githubUpdate;
    up["checked"] = gu.checked;
    up["checking"] = gu.checking;
    up["request_id"] = gu.installRequest;
    up["version"] = gu.version;
    up["installing"] = gu.installing;
    up["progress"] = gu.progress;
    up["retry_in_s"] = gu.retrySeconds;
    up["supported"] = esp_ota_get_next_update_partition(nullptr) != nullptr;
    up["release_url"] = String("https://github.com/" GITHUB_REPO "/releases/") + (gu.version[0] ? String("tag/v") + gu.version : "latest");
    up["error"] = gu.error;
    up["notes"] = gu.installing ? String(gu.progress) + " %" : String(gu.error);

    String body;
    body.reserve(measureJson(j));
    serializeJson(j, body);
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("Access-Control-Allow-Origin", "*");  // read by the other miners' dashboards
    server.send(200, "application/json", body);
}

void handleHistory() {
    uint32_t revision;
    int n = minerHistoryPoints(historyScratch, HISTORY_LEN, &revision);
    String body;
    body.reserve(n * 32 + 160);
    body = String("{\"boot_id\":\"") + String(bootSession, HEX) + "\",\"interval_s\":" + HISTORY_INTERVAL_S + ",\"revision\":" + revision;
    const char *keys[] = {"hashrate", "temperature_c", "power_percent", "coverage_percent"};
    for (int field = 0; field < 4; ++field) {
        body += String(",\"") + keys[field] + "\":[";
        for (int i = 0; i < n; ++i) {
            if (i) body += ',';
            const auto &p = historyScratch[i];
            if (field == 0) body += String(p.hashrate, 0);
            else if (field == 1) body += p.temperatureDeciC == INT16_MIN ? String("null") : String(p.temperatureDeciC / 10.0f, 1);
            else if (field == 2) body += p.coveragePercent ? String(p.powerPercent) : String("null");
            else body += String(p.coveragePercent);
        }
        body += ']';
    }
    body += '}';
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", body);
}

void handleDiagnostics() {
    const MinerStats stats = minerStats();
    MinerProfile workers[MAX_MINERS];
    minerProfiles(workers, MAX_MINERS);
    TlsProfile tls = tlsProfile();
    JsonDocument j;
    j["uptime_s"] = minerUptimeSeconds();
    j["firmware"] = FW_VERSION;
    j["tls"]["handshakes"] = tls.handshakes;
    j["boot_id"] = String(bootSession, HEX);
    j["tls"]["failures"] = tls.failures;
    j["tls"]["total_ms"] = tls.handshakeMs;
    j["tls"]["last_ms"] = tls.lastMs;
    JsonArray engines = j["engines"].to<JsonArray>();
    for (int i = 0; i < stats.minerCount; ++i) {
        if (!strcmp(stats.engine[i], "fill")) continue; // fill shares its hardware worker's time
        JsonObject engine = engines.add<JsonObject>();
        engine["index"] = i;
        engine["name"] = stats.engine[i];
        engine["hashing_ms"] = workers[i].hashingUs / 1000;
        engine["rest_ms"] = workers[i].restMs;
        engine["waiting_ms"] = workers[i].waitingMs;
        engine["paused_ms"] = workers[i].pausedMs;
    }
    String body;
    serializeJson(j, body);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", body);
}

// Every setting's value, under its key.
void settingValues(JsonObject values) {
    Settings visible = settings;
    visible.brightness = settingsBrightness();
    for (size_t i = 0; i < kSettingCount; i++) {
        const SettingInfo &s = kSettings[i];
        const void *value = settingValue(s, visible);
        switch (s.type) {
            case SettingType::Text:
            case SettingType::Secret: values[s.key] = *(const String *)value; break;
            case SettingType::Number:
                values[s.key] = s.bits == 8 ? *(const uint8_t *)value : *(const uint16_t *)value;
                break;
            case SettingType::Decimal: values[s.key] = *(const double *)value; break;
            case SettingType::Bool: values[s.key] = *(const bool *)value; break;
        }
    }
}

// Stage, normalize and save known keys; -1 means a required field became empty.
int applySettings(JsonObjectConst values) {
    // Network tasks keep using immutable live strings until the reboot.
    Settings pending = settings;
    pending.brightness = settingsBrightness();
    int changed = settingsApply(pending, values);
    if (changed) {
        settingsSanitize(pending);
        if (!pending.poolHost.length() || !pending.btcAddress.length()) return -1;
        if (!settingsSave(pending, true)) return -2;
    }
    return changed;
}

void handleGetSettings() {
    if (!authorized()) return;
    static const char *kTypes[] = {"text", "secret", "number", "decimal", "bool"};
    JsonDocument j;
    j["capabilities"]["ota"] = esp_ota_get_next_update_partition(nullptr) != nullptr;
    j["capabilities"]["firmware_env"] = FW_ENV;
    JsonArray schema = j["schema"].to<JsonArray>();
    for (size_t i = 0; i < kSettingCount; i++) {
        const SettingInfo &s = kSettings[i];
        JsonArray row = schema.add<JsonArray>();
        row.add(s.key);
        row.add(kTypes[(int)s.type]);
        row.add(s.group);
        row.add(s.label);
        JsonObject limits = row.add<JsonObject>();
        if (s.type == SettingType::Number) {
            SettingLimits range = settingLimits(s);
            limits["min"] = range.minimum;
            limits["max"] = range.maximum;
        } else if (s.type == SettingType::Decimal) limits["min"] = 0;
    }
    settingValues(j["values"].to<JsonObject>());
    String body;
    serializeJson(j, body);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", body);
}

// Saves the settings present in the JSON body (the page's form) and restarts,
// which applies everything the same way as at boot.
void handlePostSettings() {
    if (!authorized()) return;
    if (restartAt || ota::busy()) {
        server.send(409, "text/plain", "Firmware update or restart pending; try again afterwards");
        return;
    }
    JsonDocument j;
    if (deserializeJson(j, server.arg("plain")) || !j.is<JsonObject>()) {
        server.send(400, "text/plain", "Not a settings object");
        return;
    }
    if (const char *error = settingsError(j.as<JsonObjectConst>())) {
        server.send(400, "text/plain", error);
        return;
    }
    int changed = applySettings(j.as<JsonObjectConst>());
    if (changed == -2) {
        server.send(500, "text/plain", "Settings could not be saved. Previous configuration kept; try again.");
        return;
    }
    if (changed < 0) {
        server.send(400, "text/plain", "Pool host and payout address cannot be empty");
        return;
    }
    if (!changed) {
        server.send(400, "text/plain", "No settings to save");
        return;
    }
    logf("[web] %d settings saved, restarting\n", changed);
    server.send(200, "text/plain", "Saved, restarting");
    restartSoon("settings changed from the web page");
}

// GET /api/backup: settings (passwords included) and lifetime statistics.
void handleGetBackup() {
    if (!authorized()) return;
    JsonDocument j;
    j["bdlminer_backup"] = 1;
    j["board"] = board::modelName();
    j["firmware"] = FW_VERSION;
    time_t now = time(nullptr);
    j["date"] = now > 1700000000 ? (uint32_t)now : 0;
    settingValues(j["settings"].to<JsonObject>());
    StatsSnapshot s;
    minerStatsSnapshot(s);
    JsonObject st = j["stats"].to<JsonObject>();
    st["best"] = s.bestDiff;
    st["hashes"] = s.hashes;
    st["shares"] = s.shares;
    st["blocks"] = s.blocks;
    st["seconds"] = s.seconds;
    JsonArray top = st["top"].to<JsonArray>();
    for (const BestShare &b : s.top) {
        if (!(b.difficulty > 0)) break;
        JsonObject o = top.add<JsonObject>();
        o["d"] = b.difficulty;
        o["t"] = b.time;
    }
    String body;
    serializeJson(j, body);
    char name[80];
    snprintf(name, sizeof(name), "attachment; filename=\"%s-backup.json\"", hostName.c_str());
    server.sendHeader("Content-Disposition", name);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", body);
}

// POST /api/backup: a file from GET /api/backup (or a settings-only export
// of an earlier version); restores what it holds, then restarts.
void handlePostBackup() {
    if (!authorized()) return;
    if (restartAt || ota::busy()) {
        server.send(409, "text/plain", "Firmware update or restart pending; try again afterwards");
        return;
    }
    JsonDocument j;
    if (deserializeJson(j, server.arg("plain")) || !j.is<JsonObject>()) {
        server.send(400, "text/plain", "Not a backup file");
        return;
    }
    if (const char *error = webvalidation::backupError(j.as<JsonObjectConst>(), TOP_SHARES)) {
        server.send(400, "text/plain", error);
        return;
    }
    bool full = j["bdlminer_backup"].is<unsigned>();
    JsonObjectConst values = full ? j["settings"].as<JsonObjectConst>() : j.as<JsonObjectConst>();
    if (const char *error = settingsError(values)) {
        server.send(400, "text/plain", error);
        return;
    }
    int settingsCount = applySettings(values);
    if (settingsCount == -2) {
        server.send(500, "text/plain", "Settings could not be saved. Previous configuration kept; try again.");
        return;
    }
    if (settingsCount < 0) {
        server.send(400, "text/plain", "Pool host and payout address cannot be empty");
        return;
    }
    JsonObjectConst st = j["stats"];
    bool withStats = full && !st.isNull();
    if (withStats) {
        StatsSnapshot s{};
        s.bestDiff = st["best"] | 0.0;
        s.hashes = st["hashes"] | (uint64_t)0;
        s.shares = st["shares"] | 0u;
        s.blocks = st["blocks"] | 0u;
        s.seconds = st["seconds"] | 0u;
        int i = 0;
        for (JsonObjectConst o : st["top"].as<JsonArrayConst>()) {
            if (i == TOP_SHARES) break;
            s.top[i++] = {o["d"] | 0.0, o["t"] | 0u};
        }
        // backupError() tolerates the order JSON rounding may change, but
        // the miner inserts new records into a list it expects best first.
        std::stable_sort(s.top, s.top + i, [](const BestShare &a, const BestShare &b) {
            return a.difficulty > b.difficulty;
        });
        if (!minerStatsRestore(s)) {
            server.send(500, "text/plain", "Settings saved, but statistics could not be restored. Retry before restarting.");
            return;
        }
    }
    if (!settingsCount && !withStats) {
        server.send(400, "text/plain", "Nothing to restore in this file");
        return;
    }
    logf("[web] backup restored: %d settings%s, restarting\n", settingsCount, withStats ? " and statistics" : "");
    server.send(200, "text/plain", String("Restored ") + settingsCount + " settings" +
                                       (withStats ? " and the statistics" : "") + ", restarting… reload in 15 s");
    restartSoon("backup restored from the web page");
}

// POST /update (multipart): one firmware.bin built for this board.
bool uploadStarted = false, uploadComplete = false;
bool updateAllowed = false, updateBusy = false, uploadRejected = false;
String uploadError;

void resetUpload() {
    uploadStarted = uploadComplete = updateAllowed = updateBusy = uploadRejected = false;
    uploadError = "";
}

void abortWebUpload() {
    if (uploadStarted && updateAllowed && !restartAt && ota::owns(ota::Owner::Web)) {
        if (Update.isRunning()) Update.abort();
        ota::release(ota::Owner::Web);
    }
}

void uploadFailure(int status, const String &message) {
    abortWebUpload();
    server.send(status, "text/plain", message);
    resetUpload();
}

void handleUpload() {
    HTTPUpload &up = server.upload();
    if (up.status == UPLOAD_FILE_WRITE) vTaskDelay(1);
    if (up.status == UPLOAD_FILE_ABORTED) {
        abortWebUpload();
        resetUpload();
        return;
    }
    if (up.status == UPLOAD_FILE_START) {
        if (uploadStarted) {
            uploadRejected = true;
            uploadComplete = false;
            uploadError = "Only one firmware file is allowed";
            abortWebUpload();
            return;
        }
        resetUpload();
        uploadStarted = true;
        updateAllowed = !settings.webPassword.isEmpty() && server.authenticate("admin", settings.webPassword.c_str());
        if (!updateAllowed) return;
        if (restartAt) { updateBusy = true; return; }
        String name = up.filename;
        name.toLowerCase();
        if (!name.endsWith(".bin") || name.endsWith("factory.bin")) {
            uploadRejected = true;
            uploadError = "Use a firmware .bin file, not a factory image";
            return;
        }
        if (!ota::acquire(ota::Owner::Web)) { updateBusy = true; return; }
        logf("[web] firmware update: %s\n", up.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            uploadError = Update.errorString();
            ota::release(ota::Owner::Web);
        }
        return;
    }
    if (!updateAllowed || updateBusy || uploadRejected || !ota::owns(ota::Owner::Web)) return;
    if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize) {
            uploadError = Update.errorString();
            abortWebUpload();
        }
    } else if (up.status == UPLOAD_FILE_END) {
        if (!up.totalSize) {
            uploadRejected = true;
            uploadError = "Empty firmware file";
            abortWebUpload();
        } else {
            // Activate the new image only after the entire multipart request
            // is parsed and authenticated. A later part or abort can still fail.
            uploadComplete = true;
        }
    }
}

void handleUpdateDone() {
    // Authenticate this request even if it has no multipart callbacks. Never
    // reuse authorization or success flags from an earlier upload.
    if (!authorized()) { abortWebUpload(); resetUpload(); return; }
    if (restartAt || updateBusy) {
        uploadFailure(409, "Another firmware update or restart is already pending");
        return;
    }
    if (!uploadStarted) { uploadFailure(400, "No firmware file uploaded"); return; }
    if (uploadRejected) { uploadFailure(400, uploadError); return; }
    if (!uploadComplete || !uploadError.isEmpty() || !ota::owns(ota::Owner::Web)) {
        String why = uploadError.isEmpty() ? String("Incomplete firmware upload") : uploadError;
        uploadFailure(500, String("Update failed: ") + why);
        return;
    }
    if (!Update.end(true)) {
        String why = Update.errorString();
        uploadFailure(500, String("Update failed: ") + why +
                          " (this board may require a USB update)");
        return;
    }
    logf("[web] firmware validated, restarting\n");
    resetUpload();
    server.send(200, "text/plain", "Firmware updated, restarting… reload in 20 s");
    restartSoon("firmware updated from the web page");
    // Keep OTA ownership until restart so neither miners nor another updater
    // can modify the newly selected image in the response/reboot interval.
}

// Prometheus text format, for Grafana / Home Assistant scrapers.
void handleMetrics() {
    const MinerStats stats = minerStats();
    String out;
    out.reserve(1536);
    auto metric = [&out](const char *name, const char *type, double value, const char *labels = "") {
        char line[160];
        if (type) {
            snprintf(line, sizeof(line), "# TYPE %s %s\n", name, type);
            out += line;
        }
        snprintf(line, sizeof(line), "%s%s %.17g\n", name, labels, value);
        out += line;
    };
    metric("bdlminer_hashrate_hps", "gauge", minerHashrate());
    metric("bdlminer_hashes_total", "counter", (double)minerTotalHashes());
    metric("bdlminer_shares_accepted_total", "counter", stats.accepted);
    metric("bdlminer_shares_rejected_total", "counter", stats.rejected);
    metric("bdlminer_blocks_found_total", "counter", stats.blocksFound);
    metric("bdlminer_best_difficulty", "gauge", stats.sessionBestDiff);
    metric("bdlminer_best_difficulty_ever", "gauge", stats.bestDiff);
    metric("bdlminer_shares_32bit_total", "counter", stats.over32bits);
    metric("bdlminer_shares_64bit_total", "counter", stats.over64bits);
    metric("bdlminer_lifetime_hashes_total", "counter", (double)stats.lifetimeHashes);
    metric("bdlminer_lifetime_shares_total", "counter", stats.lifetimeShares);
    metric("bdlminer_lifetime_seconds_total", "counter", stats.lifetimeSeconds);
    metric("bdlminer_pool_connected", "gauge", stats.poolConnected ? 1 : 0);
    metric("bdlminer_pool_backup", "gauge", netOnBackup() ? 1 : 0);
    metric("bdlminer_pool_difficulty", "gauge", stats.poolDiff);
    metric("bdlminer_block_height", "gauge", stats.blockHeight);
    metric("bdlminer_network_difficulty", "gauge", stats.networkDiff);
    metric("bdlminer_uptime_seconds", "gauge", minerUptimeSeconds());
    if (!isnan(board::temperature())) metric("bdlminer_temperature_celsius", "gauge", board::temperature());
    metric("bdlminer_throttle", "gauge", (int)minerThrottle());
    metric("bdlminer_power_percent", "gauge", minerPowerPercent());
    metric("bdlminer_wifi_rssi_dbm", "gauge", WiFi.RSSI());
    metric("bdlminer_free_heap_bytes", "gauge", ESP.getFreeHeap());
    for (int i = 0; i < stats.minerCount; i++) {
        char labels[40];
        // Software fallback can run two engines with the same name.
        snprintf(labels, sizeof(labels), "{engine=\"%s\",index=\"%d\"}", stats.engine[i], i);
        // A metric family has one TYPE line, even with several engines.
        metric("bdlminer_engine_hashrate_hps", i == 0 ? "gauge" : nullptr, minerEngineRate(i), labels);
    }
    char labels[96];
    snprintf(labels, sizeof(labels), "{board=\"%s\",firmware=\"%s\"}", board::modelName(), FW_VERSION);
    metric("bdlminer_info", "gauge", 1, labels);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/plain; version=0.0.4", out);
}

void webTask(void *) {
    for (;;) {
        server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

}  // namespace

void webStart() {
    bootSession = esp_random();
    char name[24];
    snprintf(name, sizeof(name), "bdlminer-%04x", (unsigned)((ESP.getEfuseMac() >> 32) & 0xFFFF));
    hostName = name;
    if (MDNS.begin(name)) MDNS.addService("http", "tcp", 80);

    server.on("/assets/style.css", HTTP_GET, []() { sendAsset("text/css; charset=utf-8", kWebStyle, sizeof(kWebStyle)); });
    server.on("/assets/common.js", HTTP_GET, []() { sendAsset("text/javascript; charset=utf-8", kWebCommon, sizeof(kWebCommon)); });
    server.on("/assets/dashboard.js", HTTP_GET, []() { sendAsset("text/javascript; charset=utf-8", kWebDashboard, sizeof(kWebDashboard)); });
    server.on("/assets/settings.js", HTTP_GET, []() { sendAsset("text/javascript; charset=utf-8", kWebSettings, sizeof(kWebSettings)); });
    server.on("/assets/log.js", HTTP_GET, []() { sendAsset("text/javascript; charset=utf-8", kWebLog, sizeof(kWebLog)); });
    server.on("/", HTTP_GET, []() { sendPage("BDLMiner V1", kDashboard, "dashboard"); });
    server.on("/settings", HTTP_GET, []() { sendPage("BDLMiner settings", kSettingsPage, "settings"); });
    server.on("/log", HTTP_GET, []() { sendPage("BDLMiner log", kLogPage, "log"); });
    server.on("/api/stats", HTTP_GET, handleStats);
    server.on("/api/history", HTTP_GET, handleHistory);
    server.on("/api/diagnostics", HTTP_GET, handleDiagnostics);
    server.on("/api/log", HTTP_GET, []() {
        server.sendHeader("Cache-Control", "no-store");
        server.send(200, "text/plain", logText());
    });
    server.on("/api/settings", HTTP_GET, handleGetSettings);
    server.on("/api/settings", HTTP_POST, handlePostSettings);
    server.on("/api/backup", HTTP_GET, handleGetBackup);
    server.on("/api/backup", HTTP_POST, handlePostBackup);
    server.on("/api/restart", HTTP_POST, []() {
        if (!authorized()) return;
        if (restartAt || ota::busy()) {
            server.send(409, "text/plain", "Firmware update or restart pending; try again afterwards");
            return;
        }
        server.send(202, "text/plain", "Restarting");
        restartSoon("restart requested from the web page");
    });
    server.on("/update", HTTP_POST, handleUpdateDone, handleUpload);
    server.on("/api/github-check", HTTP_POST, []() {
        if (!settings.webPassword.isEmpty() && !authorized()) return;
        if (!settings.githubUpdate) {
            server.send(409, "text/plain", "GitHub updates are disabled in settings");
            return;
        }
        if (ghUpdateCheck()) {
            server.send(202, "text/plain", "Check requested");
            return;
        }
        GithubUpdate state = ghUpdateState();
        // A check already queued or running answers this request as well.
        if (state.installing || ota::busy()) server.send(409, "text/plain", "An update is being installed");
        else server.send(202, "text/plain", "Check requested");
    });
    server.on("/api/github-update", HTTP_POST, []() {
        // Only installs the latest release, and only when it carries the
        // release key's signature: without a web password (read-only
        // dashboard) it is still allowed.
        if (!settings.webPassword.isEmpty() && !authorized()) return;
        if (ghUpdateInstall()) server.send(200, "text/plain", "Installing, the miner restarts by itself");
        else server.send(409, "text/plain", "No update to install");
    });
    server.on("/metrics", HTTP_GET, handleMetrics);
    server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
    server.begin();
    // Slow HTTP clients must never stop the loop that owns the I2C sensors,
    // thermal control, buttons and screen. The worker uses cached snapshots.
    // Share core 0 with its software miner rather than preempting it for an
    // entire HTTP response. The hardware miner stays alone on core 1.
    if (!runtime::started(xTaskCreatePinnedToCore(webTask, "web", 6144, nullptr, 1, nullptr, 0) == pdPASS,
                          runtime::Web, "web")) {
        server.stop();
        logf("[web] not enough memory to start the web task\n");
        return;
    }
    logf("[web] dashboard: http://%s/ or http://%s.local/\n", WiFi.localIP().toString().c_str(), name);
}

void webHandle() {
    if (restartAt && (int32_t)(millis() - restartAt) >= 0) {
        // A validated web upload deliberately retains its OTA ownership.
        // Other reboots must reserve the slot without interrupting GitHub OTA.
        if (!ota::owns(ota::Owner::Web) && !ota::acquire(ota::Owner::Restart)) return;
        minerSaveStats();  // does nothing after a restored backup
        setRestartReason(restartWhy.load());
        ESP.restart();
    }
}

const String &webHostName() { return hostName; }
