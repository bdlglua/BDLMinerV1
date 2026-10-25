#include "net.h"

#include <WiFi.h>

#include <string>

#include "core/stratum.h"
#include "log.h"
#include "miner.h"
#include "settings.h"

namespace {

constexpr uint32_t kPoolSilenceTimeoutMs = 5 * 60 * 1000;  // no message -> reconnect
constexpr size_t kMaxLine = 16 * 1024;
// Failover: after this many sessions in a row without a successful
// authorization the backup pool is used, and the main pool is retried
// once the backup has been mined on for kBackupSessionMs.
constexpr int kFailoverAfter = 3;
constexpr uint32_t kBackupSessionMs = 30 * 60 * 1000;

char status[48] = "Starting";
volatile bool onBackup = false;

void setStatus(const char *s) {
    strlcpy(status, s, sizeof(status));
    logf("[pool] %s\n", s);
}

bool sendLine(WiFiClient &client, const std::string &line) {
    return client.write((const uint8_t *)line.data(), line.size()) == line.size();
}

bool sessionTimedOut = false;  // the last session ended because of its `maxMs`

// Some pools only let known miners subscribe (pool.nerdminers.org:
// "Only allowed user agents may subscribe"). After such a refusal the
// following sessions present themselves as NerdMinerV2, until a restart.
constexpr const char *kFallbackAgent = "NerdMinerV2";
bool useFallbackAgent = false;

const char *userAgent() { return useFallbackAgent ? kFallbackAgent : settings.userAgent.c_str(); }

// Runs one pool session until the connection drops, or until `maxMs` has
// elapsed when it is not 0.
void session(WiFiClient &client, stratum::Protocol &proto, uint32_t maxMs) {
    std::string buf;
    uint8_t chunk[512];
    uint32_t start = millis();
    uint32_t lastRx = start;

    if (!sendLine(client, proto.subscribe(userAgent()))) return;

    while (client.connected() && WiFi.isConnected()) {
        int n;
        while ((n = client.read(chunk, sizeof(chunk))) > 0) {
            lastRx = millis();
            for (int i = 0; i < n; i++) {
                char c = (char)chunk[i];
                if (c != '\n') {
                    if (buf.size() < kMaxLine) buf += c;
                    continue;
                }
                if (buf.empty()) continue;

                stratum::Event ev = proto.handleLine(buf.c_str());
                buf.clear();
                switch (ev) {
                    case stratum::Event::Subscribed:
                        if (settings.suggestedDifficulty > 0 &&
                            !sendLine(client, proto.suggestDifficulty(settings.suggestedDifficulty))) return;
                        if (!sendLine(client, proto.authorize())) return;
                        break;
                    case stratum::Event::SubscribeFailed:
                        setStatus("Subscribe refused");
                        logf("[pool] subscribe refused: %s (user agent \"%s\")\n", proto.lastError().c_str(),
                             userAgent());
                        if (!useFallbackAgent && strcasestr(proto.lastError().c_str(), "agent") &&
                            settings.userAgent != kFallbackAgent) {
                            useFallbackAgent = true;
                            logf("[pool] retrying with user agent \"%s\"\n", kFallbackAgent);
                        }
                        return;
                    case stratum::Event::Authorized:
                        stats.poolConnected = true;
                        setStatus(onBackup ? "Mining (backup pool)" : "Mining");
                        break;
                    case stratum::Event::AuthFailed:
                        setStatus("Auth failed: check address");
                        logf("[pool] error: %s\n", proto.lastError().c_str());
                        return;
                    case stratum::Event::NewJob:
                        if (!proto.hasJob()) break;
                        minerPublishJob(proto.job(), proto.job().clean);
                        logf("[pool] job %s height=%u clean=%d diff=%g\n", proto.job().id.c_str(),
                             proto.job().blockHeight(), proto.job().clean, proto.job().difficulty);
                        break;
                    case stratum::Event::Difficulty:
                        stats.poolDiff = proto.difficulty();
                        logf("[pool] difficulty %g\n", proto.difficulty());
                        break;
                    case stratum::Event::ShareAccepted:
                        stats.accepted++;
                        logf("[pool] share accepted\n");
                        break;
                    case stratum::Event::ShareRejected:
                        stats.rejected++;
                        logf("[pool] share rejected: %s\n", proto.lastError().c_str());
                        break;
                    default:
                        break;
                }
            }
        }

        if (millis() - lastRx > kPoolSilenceTimeoutMs) {
            setStatus("Pool silent, reconnecting");
            return;
        }
        if (maxMs && millis() - start > maxMs) {
            setStatus("Switching pool");
            sessionTimedOut = true;
            return;
        }
        // Waiting for a share also paces the socket polling.
        stratum::Share share;
        if (minerNextShare(share, 20)) {
            // A clean job voids every older one; shares found on those by a
            // batch still running when it came would only be rejected.
            if (proto.job().clean && strncmp(proto.job().id.c_str(), share.jobId, sizeof(share.jobId) - 1) != 0) {
                logf("[pool] stale share dropped (job %s)\n", share.jobId);
                continue;
            }
            logf("[pool] submit nonce=%08x diff=%.4g job=%s\n", (unsigned)share.nonce, share.difficulty,
                 share.jobId);
            if (!sendLine(client, proto.submit(share))) return;
        }
    }
}

void netTask(void *) {
    WiFiClient client;
    stratum::Protocol proto;
    uint32_t backoffMs = 2000;
    int failures = 0;

    for (;;) {
        if (!WiFi.isConnected()) {
            setStatus("WiFi lost, reconnecting");
            // The WiFi driver reconnects by itself; only restart the attempt
            // when it takes too long (restarting cancels one in progress).
            // After a minute, also the fallback networks of the settings.
            uint32_t since = millis(), lost = since;
            while (!WiFi.isConnected()) {
                if (millis() - since > 30000) {
                    if (millis() - lost > 60000 ? !wifiTryFallbacks() : true) WiFi.reconnect();
                    since = millis();
                }
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }

        // With a rotation period both pools get that long in turn; otherwise
        // the backup only gets kBackupSessionMs before the main pool is retried.
        bool rotate = settings.rotateHours && !settings.backupHost.isEmpty();
        uint32_t maxMs = rotate ? settings.rotateHours * 3600000UL : onBackup ? kBackupSessionMs : 0;

        char msg[48];
        snprintf(msg, sizeof(msg), "Connecting %s", netPoolHost().c_str());
        setStatus(msg);
        bool authorized = false;
        sessionTimedOut = false;
        if (client.connect(netPoolHost().c_str(), netPoolPort(), 10000)) {
            client.setNoDelay(true);
            proto.reset();
            proto.setCredentials(settings.btcAddress.c_str(), settings.poolPassword.c_str());
            setStatus("Subscribing");
            uint32_t t0 = millis();
            session(client, proto, maxMs);
            client.stop();
            authorized = proto.authorized();
            if (millis() - t0 > 60000) backoffMs = 2000;  // session was healthy
        } else {
            setStatus("Pool unreachable");
        }

        stats.poolConnected = false;
        minerInvalidate();
        if (strncmp(status, "Mining", 6) == 0) setStatus("Disconnected");

        failures = authorized ? 0 : failures + 1;
        if (rotate && sessionTimedOut) {
            onBackup = !onBackup;
            failures = 0;
            backoffMs = 2000;
            logf("[pool] rotation: switching to %s\n", netPoolHost().c_str());
        } else if (onBackup) {
            onBackup = false;  // back to the main pool, whatever happened
            failures = 0;
        } else if (failures >= kFailoverAfter && !settings.backupHost.isEmpty()) {
            onBackup = true;
            failures = 0;
            backoffMs = 2000;
            logf("[pool] main pool failed %d times, switching to backup %s:%u\n", kFailoverAfter,
                 settings.backupHost.c_str(), settings.backupPort);
        }
        vTaskDelay(pdMS_TO_TICKS(backoffMs));
        backoffMs = min<uint32_t>(backoffMs * 2, 60000);
    }
}

}  // namespace

void netStart() { xTaskCreatePinnedToCore(netTask, "stratum", 12288, nullptr, 3, nullptr, 0); }

const char *netStatus() { return status; }

const String &netPoolHost() { return onBackup ? settings.backupHost : settings.poolHost; }

uint16_t netPoolPort() { return onBackup ? settings.backupPort : settings.poolPort; }

bool netOnBackup() { return onBackup; }
