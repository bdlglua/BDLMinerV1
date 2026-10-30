#include "ui.h"
#include "board.h"

#if defined(HAS_DISPLAY) && !defined(BOARD_GAMMA601)
#include <WiFi.h>
#include <cmath>
#include <memory>
#include <new>
#include "core/work.h"
#include "ghupdate.h"
#include "i18n.h"
#include "market.h"
#include "miner.h"
#include "net.h"
#include "settings.h"
#include "ui_view.h"
#include "watch.h"

namespace ui {
namespace {
using namespace ui_view;
const uint32_t kThemes[] = {0xF7931A, 0x38D07A, 0x3B9CF6, 0xA66BF5, 0xEC5AA0};
const uint8_t kBrightnessSteps[] = {100, 60, 30, 10, 0};
lgfx::LovyanGFX *lcd = nullptr;
std::unique_ptr<View> view;
Model model;
int screen = 0;
uint32_t screenSince = 0, flashUntil = 0;
bool shareFlash = false;
uint8_t backlight = 255;
Point samples[240], chronological[240];
int sampleCount = 0, sampleHead = 0;
uint32_t lastSample = 0, graphRevision = 0;
bool sampled = false;
uint32_t foundHeight = 0;
bool alertShown = false;

uint32_t accent() { return kThemes[settings.theme % (sizeof(kThemes) / sizeof(kThemes[0]))]; }

String diffText(double value) {
    if (!std::isfinite(value) || value <= 0) return "-";
    if (value < 1) {
        char out[24]; snprintf(out, sizeof(out), "%.5f", value); return out;
    }
    return work::formatSI(value, 2).c_str();
}

String rateText(double rate, const char **unit) {
    if (!std::isfinite(rate) || rate < 0) rate = 0;
    *unit = "H/s";
    if (rate >= 1e12) { rate /= 1e12; *unit = "TH/s"; }  // ASIC boards
    else if (rate >= 1e9) { rate /= 1e9; *unit = "GH/s"; }
    else if (rate >= 1e6) { rate /= 1e6; *unit = "MH/s"; }
    else if (rate >= 1e3) { rate /= 1e3; *unit = "kH/s"; }
    char out[24];
    snprintf(out, sizeof(out), rate >= 100 ? "%.1f" : rate >= 10 ? "%.2f" : "%.3f", rate);
    return out;
}

String duration(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) return "-";
    char out[32];
    if (seconds < 60) snprintf(out, sizeof(out), "%.0fs", seconds);
    else if (seconds < 3600) snprintf(out, sizeof(out), "%.0fmin", seconds / 60);
    else if (seconds < 86400) snprintf(out, sizeof(out), "%uh%02u", unsigned(seconds / 3600), unsigned(seconds / 60) % 60);
    else if (seconds < 31557600) snprintf(out, sizeof(out), tr("%.0fd %.0fh"), floor(seconds / 86400), fmod(seconds / 3600, 24));
    else snprintf(out, sizeof(out), tr("%s yr"), work::formatSI(seconds / 31557600, 1).c_str());
    return out;
}

String priceText(double price) {
    if (!std::isfinite(price) || price <= 0) return "-";
    char out[24]; snprintf(out, sizeof(out), "%.0f", price); return out;
}

void metric(int i, const char *label, const String &value, const String &detail = String(), uint32_t color = kText) {
    auto &m = model.metrics[i];
    m.label = tr(label); m.value = value.c_str(); m.detail = detail.c_str(); m.color = color;
}

// The miner state shown in the header when it is not simply mining.
const char *limitState() {
    const char *reason = watchLimitReason();
    if (!strcmp(reason, "thermal") || !strcmp(reason, "hot")) return "COOLING";
    if (!strcmp(reason, "sensor")) return "NO SENSOR";
    if (!strcmp(reason, "battery")) return "BATTERY";
    if (!minerPowerPercent()) return "PAUSED";
    if (minerPowerPercent() < 100) return "LIMITED";
    return nullptr;
}

String shortDuration(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) return "-";
    char out[24];
    if (seconds < 3600) snprintf(out, sizeof(out), "%.0fmin", seconds / 60);
    else if (seconds < 86400) snprintf(out, sizeof(out), "%uh", unsigned(seconds / 3600));
    else snprintf(out, sizeof(out), tr("%.0fd %.0fh"), floor(seconds / 86400), fmod(seconds / 3600, 24));
    return out;
}

// metrics[0] is the page's big value (detail = unit), [1..3] the readings under it.
void buildModel(const MinerStats &stats, uint32_t now) {
    model.page = screen; model.language = settings.language; model.accent = accent();
    model.status = tr(stats.poolConnected ? "LIVE" : "OFFLINE");
    model.statusColor = stats.poolConnected ? kGreen : kRed;
    if (const char *state = limitState()) { model.status = tr(state); model.statusColor = kAmber; }
    int battery = board::batteryLevel();
    model.battery = battery >= 0 ? (String(battery) + "%").c_str() : "";
    if (shareFlash && int32_t(flashUntil - now) > 0) { model.status = tr("+1 SHARE"); model.statusColor = kAmber; }
    else shareFlash = false;
    model.footer = "";
    for (auto &m : model.metrics) m = Metric();
    MarketData market = marketGet();
    switch (screen) {
        case 0: {
            model.title = tr("MINING");
            const char *unit; String rate = rateText(minerHashrateLive(), &unit);
            metric(0, "LIVE HASHRATE", rate, unit, accent());
            metric(1, "SHARES", String(stats.accepted), String(), stats.rejected ? kAmber : kGreen);
            metric(2, "BEST DIFF", diffText(stats.sessionBestDiff));
            float t = board::temperature();
            uint32_t color = !std::isfinite(t) ? kMuted : settings.maxTemp && t >= settings.maxTemp ? kRed
                              : settings.maxTemp && t >= settings.maxTemp - 10 ? kAmber : kText;
            metric(3, "TEMP", std::isfinite(t) ? String(t, 1) + " C" : String("--"), String(), color);
            break;
        }
        case 1: {
            model.title = tr("CLOCK");
            struct tm time; char clock[12] = "--:--"; String date = "-";
            if (clockNow(time)) { strftime(clock, sizeof(clock), "%H:%M", &time); date = trDate(time); }
            int year = date.lastIndexOf(' ');  // "lun 26 oct 2026" -> "lun 26 oct"
            if (year > 0) date = date.substring(0, year);
            metric(0, "LOCAL TIME", clock);
            metric(1, "DATE", date);
            metric(2, "BTC PRICE", priceText(market.price), String(), accent());
            metric(3, "BLOCK", stats.blockHeight ? String(stats.blockHeight) : String("-"));
            break;
        }
        case 2: {
            model.title = tr("BITCOIN");
            metric(0, "BITCOIN PRICE", priceText(market.price), settings.currency, accent());
            metric(1, "FEES sat/vB", market.feeFast ? String(market.feeFast) + "/" + market.feeHalfHour + "/" + market.feeHour : String("-"));
            char retarget[20] = "-";
            if (market.updatedMs) snprintf(retarget, sizeof(retarget), "%+.2f%%", market.retargetChange);
            metric(2, "NEXT RETARGET", retarget, String(), !market.updatedMs ? kMuted : market.retargetChange >= 0 ? kGreen : kRed);
            metric(3, "NET HASHRATE", market.networkHashrate > 0 ? String(work::formatSI(market.networkHashrate, 1).c_str()) + "H/s" : String("-"));
            break;
        }
        case 3: {
            model.title = tr("NETWORK");
            metric(0, "BLOCK HEIGHT", stats.blockHeight ? String(stats.blockHeight) : String("-"), String(), accent());
            metric(1, "NETWORK DIFF", diffText(stats.networkDiff));
            metric(2, netOnBackup() ? "BACKUP POOL" : "POOL", netPoolHost(), String(), netOnBackup() ? kAmber : kText);
            metric(3, "HALVING", stats.blockHeight ? String((stats.blockHeight / 210000 + 1) * 210000 - stats.blockHeight) : String("-"));
            break;
        }
        case 4: {
            model.title = tr("DEVICE");
            metric(0, "LOCAL IP", WiFi.localIP().toString());
            metric(1, "BOARD", board::modelName());
            metric(2, "FIRMWARE", "v" FW_VERSION);
            metric(3, "FREE RAM", String(ESP.getFreeHeap() / 1024) + " KB");
            break;
        }
        case 5: {
            model.title = tr("HASHRATE"); float lo = 0, hi = 0, sum = 0; int valid = 0;
            for (int i = 0; i < sampleCount; ++i) {
                chronological[i] = samples[(sampleHead - sampleCount + i + 240) % 240];
                float v = chronological[i].rate;
                if (!std::isfinite(v) || v < 0) continue;
                if (!valid) lo = hi = v;
                lo = std::min(lo, v); hi = std::max(hi, v); sum += v; ++valid;
            }
            metric(0, "AVG H/s", valid ? String(work::formatSI(sum / valid, 1).c_str()) : String("-"), "H/s", accent());
            metric(1, "MIN", valid ? String(work::formatSI(lo, 1).c_str()) : String("-"));
            metric(2, "MAX", valid ? String(work::formatSI(hi, 1).c_str()) : String("-"));
            model.points = chronological; model.pointCount = sampleCount; model.graphRevision = graphRevision;
            break;
        }
        case 6: {
            model.title = tr("RECORDS");
            metric(0, "BEST EVER", diffText(stats.bestDiff), String(), accent());
            metric(1, "32-BIT", String(stats.over32bits));
            metric(2, "SESSION", diffText(stats.sessionBestDiff));
            metric(3, "RUN TIME", shortDuration(stats.lifetimeSeconds));
            break;
        }
        default: {
            model.title = tr("BLOCKS");
            metric(0, "MINING BLOCK", stats.blockHeight ? String(stats.blockHeight) : String("-"), String(), accent());
            BlockBest bests[BLOCK_BESTS];
            int count = minerBlockBests(bests, BLOCK_BESTS); time_t nowTime = time(nullptr);
            for (int i = 0; i < 3 && i < market.blockCount; ++i) {
                const auto &block = market.blocks[i];
                String detail = nowTime > 1700000000 && block.time ? duration(std::max<double>(0, nowTime - double(block.time))) : String("-");
                for (int j = 0; j < count; ++j) if (bests[j].height == block.height) detail += " / " + diffText(bests[j].best);
                metric(i + 1, "", block.pool[0] ? block.pool : "?", detail);
                model.metrics[i + 1].label = String(block.height).c_str();
            }
            break;
        }
    }
}
}  // namespace

void begin() {
    lcd = board::display(); if (!lcd) return;
    lcd->setRotation(settings.rotation); backlight = settingsBrightness(); board::setBrightness(backlight);
    view.reset(new (std::nothrow) ui_view::View(lcd, true, accent(), settings.language)); screenSince = millis();
}
void showSplash() {
    if (!view) return;
    String info = String("v" FW_VERSION) + " / " + tr("Hold A at boot: setup");
    view->message("BDLMiner V1", board::modelName(), info.c_str());
}
void showPortal(const String &apName, const char *password) {
    if (!view) return;
    String qr = "WIFI:T:WPA;S:" + apName + ";P:" + password + ";;";
    view->portal(tr("WiFi setup"), apName.c_str(), password, qr.c_str());
}
void showMessage(const char *title, const char *line1, const char *line2) { if (view) view->message(tr(title), tr(line1), tr(line2)); }
void nextScreen() {
    if (alertShown) return;  // the alert stays until a button is pressed
    screen = (screen + 1) % ui_view::kPages; screenSince = millis();
    if (view) view->invalidate(); render();
}
void cycleBrightness() {
    size_t i = 0;
    while (i < sizeof(kBrightnessSteps) && kBrightnessSteps[i] != settingsBrightness()) ++i;
    settingsSetBrightness(kBrightnessSteps[(i + 1) % sizeof(kBrightnessSteps)]); setBacklight(settingsBrightness());
}
void setBacklight(uint8_t percent) {
    if (!lcd || percent == backlight) return;
    bool wasOff = backlight == 0; backlight = percent; board::setBrightness(percent);
    if (wasOff && percent && view) { view->invalidate(); render(); }
}
void applySettings() {
    if (!lcd) return;
    if (lcd->getRotation() != settings.rotation) lcd->setRotation(settings.rotation);
    screenSince = millis();
    if (view) view->invalidate();
    render();
}
void setRotation(uint8_t rotation) {
    if (!lcd || rotation == lcd->getRotation() || (rotation & 1) != (lcd->getRotation() & 1)) return;
    lcd->setRotation(rotation); if (view) view->invalidate(); render();
}
void flashShare() { shareFlash = true; flashUntil = millis() + 1600; }
void blockFound(uint32_t height) { foundHeight = height; alertShown = true; render(); }
bool dismissAlert() {
    if (!alertShown) return false;
    alertShown = false; screenSince = millis();
    if (view) view->invalidate(); render();
    return true;
}
uint32_t screenAge() { return millis() - screenSince; }
void render() {
    uint32_t now = millis();
    if (!sampled || now - lastSample >= 10000) {
        sampled = true; lastSample = now; samples[sampleHead] = {now, float(minerHashrateLive())};
        sampleHead = (sampleHead + 1) % 240; if (sampleCount < 240) ++sampleCount; ++graphRevision;
    }
    if (!view || !settingsBrightness() || !backlight) return;
    GithubUpdate update = ghUpdateState();
    if (update.installing) {
        String versions = String(FW_VERSION) + " -> " + update.version;
        view->update(tr("Automatic update"), versions.c_str(), update.progress, tr(update.progress >= 100 ? "Restarting..." : "Installing"));
        return;
    }
    if (alertShown) {
        String height = foundHeight ? String(foundHeight) : String("!");
        view->alert(tr("BLOCK FOUND!"), height.c_str(), tr("Press a button"));
        return;
    }
    buildModel(minerStats(), now); view->render(model);
}
}  // namespace ui
#elif !defined(BOARD_GAMMA601)
namespace ui {
void begin() {}
void showSplash() {}
void showPortal(const String &, const char *) {}
void showMessage(const char *, const char *, const char *) {}
void nextScreen() {}
void cycleBrightness() {}
void setBacklight(uint8_t) {}
void applySettings() {}
void setRotation(uint8_t) {}
void flashShare() {}
void blockFound(uint32_t) {}
bool dismissAlert() { return false; }
uint32_t screenAge() { return 0; }
void render() {}
}  // namespace ui
#endif
