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
#include "settings.h"
#include "tls.h"
#include "update_key.h"

#ifndef FW_ENV
#define FW_ENV "dev"
#endif

namespace {

constexpr uint32_t kFirstCheckMs = 2 * 60 * 1000;
constexpr uint32_t kCheckMs = 12 * 3600 * 1000UL;
constexpr uint32_t kRetryMs = 30 * 60 * 1000;
constexpr uint32_t kMinHeap = 70 * 1024;  // a TLS session needs ~45 KB

GithubUpdate state;
String firmwareUrl, signatureUrl;
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool installRequested = false;
uint32_t nextCheck = kFirstCheckMs;  // millis()

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
        tls.stop();
        if (!tlsConnect(tls, host.c_str())) return false;
        http.end();
        http.setReuse(false);
        http.setTimeout(15000);
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        if (!http.begin(tls, url)) return false;
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

// False when the release could not be read (retried sooner).
bool check() {
    WiFiClientSecure tls;
    HTTPClient http;
    int code = 0;
    if (!httpsGet("https://api.github.com/repos/" GITHUB_REPO "/releases/latest", tls, http, &code)) {
        setError("cannot read the latest release (HTTP %s)", String(code).c_str());
        return false;
    }
    JsonDocument filter, doc;
    filter["tag_name"] = true;
    filter["name"] = true;
    filter["assets"][0]["name"] = true;
    filter["assets"][0]["browser_download_url"] = true;
    DeserializationError err =
        http.getSize() > 0 ? deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter))
                           : deserializeJson(doc, http.getString(), DeserializationOption::Filter(filter));
    http.end();
    if (err) {
        setError("release: %s", err.c_str());
        return false;
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
    hwsha::hold();  // S2/S3/C3: TLS would wait for every mining batch
    // The download keeps core 0 busy along with the software miner: its idle
    // task, starved, would trip the watchdog (it did, mid-download).
    core0WdtPause();

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
            state.progress = (uint64_t)done * 100 / size;
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
    core0WdtResume();
    hwsha::release();

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

}  // namespace

void ghUpdatePoll() {
    if (!settings.githubUpdate || !(installRequested || (int32_t)(millis() - nextCheck) >= 0)) return;
    if (!WiFi.isConnected() || ESP.getFreeHeap() < kMinHeap) {
        static uint32_t lastLog = 0;
        if (installRequested && millis() - lastLog > 60000) {
            lastLog = millis();
            logf("[update] install waiting: %s\n", WiFi.isConnected() ? "low memory" : "no WiFi");
        }
        return;
    }
    if (!installRequested) nextCheck = millis() + (check() ? kCheckMs : kRetryMs);
    if ((installRequested || settings.githubUpdate == 2) && state.available) install();
    installRequested = false;
}

GithubUpdate ghUpdateState() {
    portENTER_CRITICAL(&stateMux);
    GithubUpdate s = state;
    portEXIT_CRITICAL(&stateMux);
    return s;
}

bool ghUpdateInstall() {
    if (!state.available || state.installing) return false;
    installRequested = true;
    return true;
}
