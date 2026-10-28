#include "watch.h"

#include <Arduino.h>

#include "board.h"
#include "core/mining_safety.h"
#include "log.h"
#include "market.h"
#include "miner.h"
#include "net.h"
#include "notify.h"
#include "ota.h"
#include "settings.h"
#include "ui.h"

namespace {

constexpr uint32_t kNoPoolRestartMs = 30 * 60 * 1000;  // not connected that long: restart
constexpr uint32_t kOfflineNotifyMs = 10 * 60 * 1000;
constexpr uint32_t kBestNotifyMs = 10 * 60 * 1000;   // at most one record notification per 10 min

std::atomic<bool> night{false};
std::atomic<const char *> limitReason{"supervision"};
mining::ThermalPolicy thermal;
bool awake = false;
uint32_t wakeStarted = 0;  // night mode suspended for 30 s after a button press

void restart(const char *why) {
    ota::Guard reboot(ota::Owner::Restart);
    if (!reboot) return; // defer automatic restarts until the active OTA finishes
    logf("[watch] restarting: %s\n", why);
    minerSaveStats();  // what was mined since the last periodic save
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
    board::sampleTemperature();
    float t = board::temperature();
    bool wasHot = thermal.hot, wasFault = thermal.sensorFault;
    bool saver = settings.batterySaver && board::onBattery() == 1;
    uint8_t power = thermal.power(t, settings.maxTemp, settings.miningPower, saver);
    const char *reason = "full";
    if (!settings.miningPower) reason = "user_pause";
    else if (thermal.sensorFault) reason = "sensor";
    else if (thermal.hot) reason = "hot";
    else if (settings.maxTemp && std::isfinite(t) && t >= settings.maxTemp - 10 && power < settings.miningPower) reason = "thermal";
    else if (saver && settings.miningPower > 50) reason = "battery";
    else if (settings.miningPower < 100) reason = "user";
    limitReason = reason;
    if (thermal.hot != wasHot) {
        if (thermal.hot) {
            char msg[64];
            snprintf(msg, sizeof(msg), "%.0f C, mining paused until %u C", t, settings.maxTemp - 5);
            logf("[watch] too hot: %s\n", msg);
            notify("Miner too hot", msg, 4, "thermometer");
        } else {
            logf("[watch] cooled down to %.0f C, mining again\n", t);
            notify("Miner cooled down", "Mining again", 2, "thermometer");
        }
    }
    if (thermal.sensorFault && !wasFault) {
        logf("[watch] temperature unavailable: mining %s\n", power ? "limited to 25%" : "paused");
        notify("Temperature unavailable", power ? "No usable sensor: mining limited to 25%" : "Mining paused until a valid temperature returns", 4, "thermometer");
    }
    minerSetPower(power, saver);
    minerObserve(t, minerPowerPercent());

    uint8_t b = settingsBrightness();
    if (awake && millis() - wakeStarted >= 30000) awake = false;
    if (night && !awake) b = 0;
    else if (saver && b > 20) b = 20;
    ui::setBacklight(b);
}

void events(uint32_t now) {
    const MinerStats stats = minerStats();
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

    // The red LED lights for a second on each 32-bit share (not at night).
    bool led = false;
    if (stats.over32bits != lastOver32) {
        lastOver32 = stats.over32bits;
        logf("[miner] 32-bit share found (%u so far)\n", (unsigned)lastOver32);
        if (settings.beep && !night) board::beep();
        led = !night;
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
    }
    // A paused miner (power 0 %, too hot, firmware update) finds no share by
    // design: that time must not count towards the restart below.
    if (!minerPowerPercent()) acceptedAt = now;
    board::flashLed(led);

    if (stats.poolConnected) {
        lastConnected = now;
        if (offlineSent) {
            offlineSent = false;
            notify("Miner back online", netStatus().c_str(), 2, "white_check_mark");
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
        snprintf(msg, sizeof(msg), "Not mining for 10 minutes: %s", netStatus().c_str());
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

const char *watchLimitReason() {
    if (ota::busy()) return "ota";
    const char *reason = limitReason.load();
    if (!minerPowerPercent() && strcmp(reason, "hot") && strcmp(reason, "sensor") && strcmp(reason, "user_pause"))
        return "supervision";
    return reason;
}

void watchWake() {
    wakeStarted = millis();
    awake = true;
}
