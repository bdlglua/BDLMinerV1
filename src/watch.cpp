#include "watch.h"

#include <Arduino.h>

#include "board.h"
#include "log.h"
#include "market.h"
#include "miner.h"
#include "net.h"
#include "notify.h"
#include "settings.h"
#include "ui.h"

namespace {

constexpr uint32_t kNoPoolRestartMs = 30 * 60 * 1000;  // not connected that long: restart
constexpr uint32_t kOfflineNotifyMs = 10 * 60 * 1000;
constexpr uint32_t kBestNotifyMs = 10 * 60 * 1000;   // at most one record notification per 10 min

bool night = false;
bool hot = false;
uint32_t wakeUntil = 0;  // night mode suspended until then (button press)

void restart(const char *why) {
    logf("[watch] restarting: %s\n", why);
    setRestartReason(why);
    delay(500);
    ESP.restart();
}

bool nightNow() {
    if (settings.nightFrom > 23 || settings.nightTo > 23 || settings.nightFrom == settings.nightTo) return false;
    struct tm t;
    if (!clockNow(t)) return false;
    int h = t.tm_hour;
    return settings.nightFrom < settings.nightTo ? h >= settings.nightFrom && h < settings.nightTo
                                                 : h >= settings.nightFrom || h < settings.nightTo;
}

void throttle() {
    float t = board::temperature();
    if (settings.maxTemp && !isnan(t)) {
        if (!hot && t >= settings.maxTemp) {
            hot = true;
            char msg[64];
            snprintf(msg, sizeof(msg), "%.0f C, mining paused until %u C", t, settings.maxTemp - 5);
            logf("[watch] too hot: %s\n", msg);
            notify("Miner too hot", msg, 4, "thermometer");
        } else if (hot && t <= settings.maxTemp - 5) {
            hot = false;
            logf("[watch] cooled down to %.0f C, mining again\n", t);
            notify("Miner cooled down", "Mining again", 2, "thermometer");
        }
    } else {
        hot = false;
    }
    bool saver = settings.batterySaver && board::onBattery() == 1;
    minerSetThrottle(hot ? Throttle::Paused : saver ? Throttle::Half : Throttle::Full);

    uint8_t b = settings.brightness;
    if (night && (int32_t)(millis() - wakeUntil) >= 0) b = 0;
    else if (saver && b > 20) b = 20;
    ui::setBacklight(b);
}

void events(uint32_t now) {
    static bool first = true;
    static uint32_t lastBlocks, lastOver32, lastAccepted, acceptedAt, lastConnected;
    static double lastBest;
    static uint32_t bestAt = 0;
    static bool offlineSent = false, bootSent = false;
    if (first) {
        first = false;
        lastBlocks = stats.blocksFound;
        lastOver32 = stats.over32bits;
        lastAccepted = stats.accepted;
        acceptedAt = lastConnected = now;
        lastBest = stats.bestDiff;
    }
    char msg[160];

    if (stats.blocksFound != lastBlocks) {
        lastBlocks = stats.blocksFound;
        snprintf(msg, sizeof(msg), "Block %u found by %s! Check the pool and your wallet.", (unsigned)stats.blockHeight,
                 board::modelName());
        notify("BLOCK FOUND", msg, 5, "tada,moneybag");
    }

    if (stats.over32bits != lastOver32) {
        lastOver32 = stats.over32bits;
        logf("[miner] 32-bit share found (%u so far)\n", (unsigned)lastOver32);
        if (settings.beep && !night) board::beep();
        if (settings.notify32) {
            snprintf(msg, sizeof(msg), "%u so far", (unsigned)lastOver32);
            notify("32-bit share", msg, 2, "sparkles");
        }
    }

    // A new all-time best of at least difficulty 1, at most every 10 minutes.
    if (stats.bestDiff > lastBest && stats.bestDiff >= 1.0 && (!bestAt || now - bestAt > kBestNotifyMs)) {
        lastBest = stats.bestDiff;
        bestAt = now;
        snprintf(msg, sizeof(msg), "New best difficulty ever: %.4g", stats.bestDiff);
        notify("New record", msg, 3, "trophy");
    }

    if (stats.accepted != lastAccepted) {
        lastAccepted = stats.accepted;
        acceptedAt = now;
        ui::flashShare();
        board::flashLed(true);
    } else {
        board::flashLed(false);
    }

    if (stats.poolConnected) {
        lastConnected = now;
        if (offlineSent) {
            offlineSent = false;
            notify("Miner back online", netStatus(), 2, "white_check_mark");
        }
        if (!bootSent) {
            bootSent = true;
            // Not after a power-up, the reset button, or a restart asked for
            // from the web page.
            if (strcmp(bootReason(), "power on") && strcmp(bootReason(), "reset pin") &&
                !strstr(bootReason(), "web page")) {
                snprintf(msg, sizeof(msg), "Last reset: %s", bootReason());
                notify("Miner restarted", msg, 3, "warning");
            }
        }
    } else if (!offlineSent && now - lastConnected > kOfflineNotifyMs) {
        offlineSent = true;
        snprintf(msg, sizeof(msg), "Not mining for 10 minutes: %s", netStatus());
        notify("Miner offline", msg, 4, "warning");
    }

    if (now - lastConnected > kNoPoolRestartMs) restart("no pool connection for 30 minutes");
    if (settings.rebootHours && now - acceptedAt > settings.rebootHours * 3600000UL) {
        snprintf(msg, sizeof(msg), "no accepted share for %u hours", settings.rebootHours);
        restart(msg);
    }
}

void screens() {
    if (settings.cycleSeconds && ui::screenAge() >= settings.cycleSeconds * 1000UL) ui::nextScreen();
    if (settings.autoRotate) {
        // Landscape either way up, from the gravity along the screen's width.
        float x, y;
        if (board::accel(x, y)) {
            if (x > 0.5f) ui::setRotation(3);
            else if (x < -0.5f) ui::setRotation(1);
        }
    }
}

}  // namespace

void watchTick() {
    uint32_t now = millis();
    night = nightNow();
    throttle();
    events(now);
    screens();
}

bool watchNight() { return night; }

void watchWake() { wakeUntil = millis() + 30000; }
