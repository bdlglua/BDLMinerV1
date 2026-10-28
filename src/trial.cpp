#include "trial.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_ota_ops.h>

#include <atomic>

#include "ghupdate.h"
#include "log.h"
#include "miner.h"
#include "notify.h"

// Arduino marks a new firmware valid at boot unless told otherwise. The trial
// below decides instead (esp32-hal-misc.c, CONFIG_APP_ROLLBACK_ENABLE).
extern "C" bool verifyRollbackLater() { return true; }

namespace {

// Accepted shares cannot be the proof: at difficulty 1 a 1 MH/s miner waits
// about 70 minutes for one. A crash or a hang during the trial restarts the
// board, and the bootloader then returns to the previous firmware by itself.
constexpr uint32_t kConfirmMs = 3 * 60 * 1000;
constexpr uint32_t kTrialMs = 20 * 60 * 1000;
constexpr uint32_t kOfflineTrialMs = 40 * 60 * 1000;  // a firmware may also break WiFi
constexpr uint8_t kTries = 2;  // a power cut during the trial also rolls back

std::atomic<bool> pending{false};

void forget() {
    Preferences p;
    if (p.begin("trial", false)) {
        p.remove("ver");
        p.remove("failver");
        p.remove("fails");
        p.end();
    }
}

}  // namespace

void trialBegin() {
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        pending = true;
        logf("[update] firmware %s on trial: confirmed after %u minutes of mining, else the previous one returns\n",
             FW_VERSION, (unsigned)(kConfirmMs / 60000));
    }

    // After a rollback the other slot is marked invalid until it is rewritten.
    Preferences p;
    p.begin("trial", true);
    String expected = p.getString("ver", "");
    String counted = p.getString("told", "");  // this rollback was already counted
    String failed = p.getString("failver", "");
    uint8_t fails = failed == expected ? p.getUChar("fails", 0) : 0;
    p.end();
    if (!esp_ota_get_last_invalid_partition() || expected.isEmpty() || expected == FW_VERSION) return;
    bool fresh = counted != expected;
    if (fresh) fails++;
    char why[80];
    snprintf(why, sizeof(why), "firmware %s failed its trial and was rolled back", expected.c_str());
    if (fails >= kTries) {
        ghUpdateRefuse(expected.c_str(), why);
        logf("[update] %s %u times; it is not installed automatically again\n", why, (unsigned)fails);
    } else {
        logf("[update] %s; it gets one more try\n", why);
    }
    if (!fresh) return;
    notify("Update rolled back", why, 4, "warning");
    Preferences w;
    if (w.begin("trial", false)) {
        w.putString("told", expected);
        w.putString("failver", expected);
        w.putUChar("fails", fails);
        w.end();
    }
}

void trialPoll() {
    if (!pending) return;
    uint32_t now = millis();
    // Hashing is not required while mining is paused on purpose (power 0 %, heat).
    bool hashing = minerHashrate() > 0 || !minerPowerPercent();
    if (now >= kConfirmMs && minerStats().poolConnected && hashing) {
        trialConfirm("connected to the pool and mining");
        return;
    }
    if (now >= (WiFi.isConnected() ? kTrialMs : kOfflineTrialMs)) {
        logf("[update] firmware %s did not prove itself in %u minutes: back to the previous one\n", FW_VERSION,
             (unsigned)(now / 60000));
        minerSaveStats();
        setRestartReason("firmware trial failed, previous firmware restored");
        delay(300);
        esp_ota_mark_app_invalid_rollback_and_reboot();  // returns only if there is nothing to go back to
        pending = false;
    }
}

void trialConfirm(const char *why) {
    if (!pending.exchange(false)) return;
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        logf("[update] firmware %s confirmed (%s)\n", FW_VERSION, why);
        forget();
    } else {
        logf("[update] firmware %s could not be confirmed\n", FW_VERSION);
    }
}

bool trialPending() { return pending; }

void trialExpect(const char *version) {
    Preferences p;
    if (p.begin("trial", false)) {
        p.putString("ver", version);
        p.remove("told");  // a new attempt: its rollback, if any, counts again
        p.end();
    }
}
