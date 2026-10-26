#include "ghupdate.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

#include "hw_sha.h"
#include "log.h"
#include "miner.h"
#include "notify.h"
#include "ota.h"
#include "settings.h"
#include "tls.h"
#include "update_key.h"

#ifndef FW_ENV
#define FW_ENV "dev"
#endif

namespace {

constexpr uint32_t kFirstCheckMs = 30 * 1000;
constexpr uint32_t kCheckMs = 3600 * 1000UL;
constexpr uint32_t kRetryMs = 5 * 60 * 1000;
constexpr uint32_t kFirstPingMs = 5 * 60 * 1000;
constexpr uint32_t kPingMs = 24 * 3600 * 1000UL;
constexpr uint32_t kMinHeap = 70 * 1024;  // a TLS session needs ~45 KB

GithubUpdate state;
String firmwareUrl, signatureUrl;
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<bool> installRequested{false};
std::atomic<bool> checkRequested{false};
uint32_t nextCheck = kFirstCheckMs;  // millis()
uint32_t nextPing = kFirstPingMs;

void setError(const char *fmt, const char *arg = "") {
    portENTER_CRITICAL(&stateMux);
    snprintf(state.error, sizeof(state.error), fmt, arg);
    portEXIT_CRITICAL(&stateMux);
    logf("[update] %s\n", state.error);
}

// "1.2.10" > "1.2.9", "1.0.1" > "1.0".
int compareVersions(const char *a, const char *b) {
    for (;;) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y) return x < y ? -1 : 1;
        bool moreA = *a == '.', moreB = *b == '.';
        if (!moreA && !moreB) return 0;
        if (moreA) a++;
        if (moreB) b++;
    }
}

// The first "1.2" or "1.2.3" in a release tag or name ("v1.0.1", "V1.0.0").
bool findVersion(const char *s, char *out, size_t size) {
    for (; s && *s; s++) {
        if (!isdigit((unsigned char)*s)) continue;
        const char *e = s;
        int dots = 0;
        while (isdigit((unsigned char)*e) || (*e == '.' && isdigit((unsigned char)e[1]) && ++dots)) e++;
        if (dots) {
            snprintf(out, size, "%.*s", (int)(e - s), s);
            return true;
        }
        s = e - 1;
    }
    return false;
}

// GET `url` over HTTPS, following redirects (GitHub sends release files to
// another host) with one tlsConnect() per host. On success `http` holds the
// response, with the body to read.
bool httpsGet(String url, WiFiClientSecure &tls, HTTPClient &http, int *code) {
    static const char *kHeaders[] = {"Location"};
    for (int hop = 0; hop < 5; hop++) {
        if (!url.startsWith("https://")) return false;
        int slash = url.indexOf('/', 8);
        String host = url.substring(8, slash < 0 ? url.length() : slash);
        http.end();
        tls.stop();
        http.setReuse(false);
        http.setTimeout(15000);
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        if (!http.begin(tls, url)) return false;
        // begin() may close the client when its host changes, including on
        // the first request. Connect afterwards so GET() reuses the guarded
        // handshake instead of starting another one with the miner running.
        if (!tlsConnect(tls, host.c_str())) return false;
        http.setUserAgent("BDLMinerV1/" FW_VERSION);
        http.addHeader("Accept", "application/vnd.github+json");
        http.collectHeaders(kHeaders, 1);
        *code = http.GET();
        if (*code >= 300 && *code < 400 && http.hasHeader("Location")) {
            url = http.header("Location");
            continue;
        }
        return *code == HTTP_CODE_OK;
    }
    return false;
}

// Prefer the small release manifest: it is served as a release asset and does
// not consume GitHub's unauthenticated API quota shared by miners on one IP.
// The API fallback keeps older releases, which have no manifest, discoverable.
bool fetchRelease(const char *url, JsonDocument &doc, String &error) {
    WiFiClientSecure tls;
    HTTPClient http;
    int code = 0;
    if (!httpsGet(url, tls, http, &code)) {
        error = "HTTP " + String(code);
        return false;
    }
    JsonDocument filter;
    filter["tag_name"] = true;
    filter["name"] = true;
    filter["assets"][0]["name"] = true;
    filter["assets"][0]["browser_download_url"] = true;
    DeserializationError err =
        http.getSize() > 0 ? deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter))
                           : deserializeJson(doc, http.getString(), DeserializationOption::Filter(filter));
    http.end();
    if (err) {
        error = err.c_str();
        return false;
    }
    return true;
}

// False when the release could not be read (retried sooner).
bool check() {
    JsonDocument doc;
    String manifestError, apiError;
    if (!fetchRelease("https://github.com/" GITHUB_REPO "/releases/latest/download/update.json", doc, manifestError)) {
        doc.clear();
        if (!fetchRelease("https://api.github.com/repos/" GITHUB_REPO "/releases/latest", doc, apiError)) {
            setError("release unavailable: %s", ("manifest " + manifestError + "; API " + apiError).c_str());
            return false;
        }
    }
    char version[24];
    if (!findVersion(doc["tag_name"] | "", version, sizeof(version)) &&
        !findVersion(doc["name"] | "", version, sizeof(version))) {
        setError("no version number in the release tag or name");
        return false;
    }
    String fw, sig;
    for (JsonObject a : doc["assets"].as<JsonArray>()) {
        String name = a["name"] | "";
        if (name == FW_ENV "-firmware.bin") fw = a["browser_download_url"] | "";
        if (name == FW_ENV "-firmware.bin.sig") sig = a["browser_download_url"] | "";
    }
    bool newer = compareVersions(version, FW_VERSION) > 0;
    bool available = newer && fw.length() && sig.length();
    bool wasAvailable;
    portENTER_CRITICAL(&stateMux);
    wasAvailable = state.available && !strcmp(state.version, version);
    state.checked = true;
    state.available = available;
    strlcpy(state.version, version, sizeof(state.version));
    state.error[0] = 0;
    portEXIT_CRITICAL(&stateMux);
    firmwareUrl = fw;
    signatureUrl = sig;
    if (newer && !available) setError("release %s has no signed firmware for " FW_ENV, version);
    logf("[update] latest release %s, this firmware %s%s\n", version, FW_VERSION,
         available ? ": update available" : "");
    if (available && !wasAvailable) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Version %s is available (this miner runs %s).", version, FW_VERSION);
        notify("Firmware update", msg, 3, "arrow_up");
    }
    return true;
}

// 0 when `sig` is a valid signature of `hash` (SHA-256) by the release key.
int verifySignature(const uint8_t hash[32], const uint8_t *sig, size_t sigLen) {
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int r = mbedtls_pk_parse_public_key(&pk, (const unsigned char *)kUpdatePublicKey, sizeof(kUpdatePublicKey));
    if (!r) r = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, 32, sig, sigLen);
    mbedtls_pk_free(&pk);
    return r;
}

void install() {
    ota::Guard update(ota::Owner::Github);
    if (!update) {
        setError("another firmware update is already running");
        return;
    }
    if (!esp_ota_get_next_update_partition(nullptr)) {
        setError("this board has no room for a second firmware: update by USB");
        return;
    }
    portENTER_CRITICAL(&stateMux);
    state.installing = true;
    state.progress = 0;
    state.error[0] = 0;
    portEXIT_CRITICAL(&stateMux);
    logf("[update] installing %s\n", state.version);
    // OTA ownership pauses miners. Only tlsConnect() takes the TLS/SHA
    // handshake locks, always in the same order; no outer SHA reservation.

    WiFiClientSecure tls;
    HTTPClient http;
    int code = 0;
    uint8_t sig[MBEDTLS_PK_SIGNATURE_MAX_SIZE];
    size_t sigLen = 0;
    bool ok = false;
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    do {
        if (!httpsGet(signatureUrl, tls, http, &code)) {
            setError("cannot download the signature (HTTP %s)", String(code).c_str());
            break;
        }
        sigLen = http.getStream().readBytes(sig, sizeof(sig));
        http.end();
        if (!httpsGet(firmwareUrl, tls, http, &code)) {
            setError("cannot download the firmware (HTTP %s)", String(code).c_str());
            break;
        }
        int size = http.getSize();
        if (size <= 0 || !Update.begin(size)) {
            setError("cannot start the update: %s", Update.errorString());
            break;
        }
        mbedtls_sha256_starts_ret(&sha, 0);
        WiFiClient *stream = http.getStreamPtr();
        static uint8_t buf[2048];
        int done = 0;
        uint32_t lastData = millis();
        while (done < size && millis() - lastData < 20000) {
            int n = stream->available() ? stream->readBytes(buf, min((int)sizeof(buf), size - done)) : 0;
            if (n <= 0) {
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            lastData = millis();
            mbedtls_sha256_update_ret(&sha, buf, n);
            if (Update.write(buf, n) != (size_t)n) break;
            done += n;
            portENTER_CRITICAL(&stateMux);
            state.progress = (uint64_t)done * 100 / size;
            portEXIT_CRITICAL(&stateMux);
            vTaskDelay(1);  // the screen and web server keep running
        }
        http.end();
        if (done != size) {
            Update.abort();
            setError("download interrupted at %s%%", String(state.progress).c_str());
            break;
        }
        uint8_t hash[32];
        mbedtls_sha256_finish_ret(&sha, hash);
        if (verifySignature(hash, sig, sigLen)) {
            Update.abort();
            setError("bad signature: update refused");
            break;
        }
        if (!Update.end(true)) {
            setError("cannot finish the update: %s", Update.errorString());
            break;
        }
        ok = true;
    } while (false);
    mbedtls_sha256_free(&sha);

    // After a success `installing` stays set: the screen keeps showing the
    // update window until the restart.
    portENTER_CRITICAL(&stateMux);
    if (ok) state.progress = 100;
    else state.installing = false;
    portEXIT_CRITICAL(&stateMux);
    if (ok) {
        char why[48];
        snprintf(why, sizeof(why), "updated to %s from GitHub", state.version);
        logf("[update] %s, restarting\n", why);
        minerSaveStats();
        setRestartReason(why);
        delay(500);
        ESP.restart();
    }
}

// The anonymous usage count: GitHub counts the downloads of each release
// file, so fetching ping-<board>.txt of this version once a day makes the
// download counts read "miner-days" per version and board
// (tools/miner_stats.py). Nothing about the miner is sent; firmware built
// locally, whose version has no release, just gets a 404.
void ping() {
    WiFiClientSecure tls;
    HTTPClient http;
    int code = 0;
    bool ok = httpsGet("https://github.com/" GITHUB_REPO "/releases/download/v" FW_VERSION "/ping-" FW_ENV ".txt",
                       tls, http, &code);
    http.end();
    logf("[update] usage count: %s\n", ok ? "counted" : String("HTTP " + String(code)).c_str());
}

}  // namespace

void ghUpdatePoll() {
    if (ota::busy()) return;
    bool due = settings.githubUpdate && (installRequested || checkRequested || (int32_t)(millis() - nextCheck) >= 0);
    if (due) {
        if (!WiFi.isConnected() || ESP.getFreeHeap() < kMinHeap) {
            static uint8_t lastWait = 0;
            uint8_t wait = WiFi.isConnected() ? 1 : 2;
            if (wait != lastWait || !ghUpdateState().error[0]) {
                setError("update waiting: %s", wait == 1 ? "low memory" : "no WiFi");
                lastWait = wait;
            }
            return;
        }
        bool requestedInstall = installRequested.exchange(false);
        bool requestedCheck = checkRequested.exchange(false);
        bool ready = true;
        if (requestedCheck || !requestedInstall) {
            portENTER_CRITICAL(&stateMux);
            state.checking = true;
            portEXIT_CRITICAL(&stateMux);
            ready = check();
            nextCheck = millis() + (ready ? kCheckMs : kRetryMs);
            portENTER_CRITICAL(&stateMux);
            state.checking = false;
            portEXIT_CRITICAL(&stateMux);
        }
        if (ready && (requestedInstall || settings.githubUpdate == 2) && ghUpdateState().available) install();
        // Metadata checks and requested installations take priority over the
        // optional usage ping. Keep this poll to one operation on the network.
        return;
    }
    if (settings.usageCount && (int32_t)(millis() - nextPing) >= 0 && WiFi.isConnected() &&
        ESP.getFreeHeap() >= kMinHeap) {
        nextPing = millis() + kPingMs;
        ping();
    }
}

GithubUpdate ghUpdateState() {
    portENTER_CRITICAL(&stateMux);
    GithubUpdate s = state;
    portEXIT_CRITICAL(&stateMux);
    s.checking = s.checking || checkRequested.load();
    s.installing = s.installing || installRequested.load();
    return s;
}

bool ghUpdateCheck() {
    GithubUpdate current = ghUpdateState();
    if (!settings.githubUpdate || current.checking || current.installing || ota::busy()) return false;
    return !checkRequested.exchange(true);
}

bool ghUpdateInstall() {
    GithubUpdate current = ghUpdateState();
    if (!settings.githubUpdate || !current.available || current.checking || current.installing || ota::busy()) return false;
    if (installRequested.exchange(true)) return false;
    portENTER_CRITICAL(&stateMux);
    ++state.installRequest;
    portEXIT_CRITICAL(&stateMux);
    return true;
}
