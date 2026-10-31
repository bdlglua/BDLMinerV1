#include "mqtt.h"

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "board.h"
#include "log.h"
#include "miner.h"
#include "net.h"
#include "settings.h"
#include "web.h"
#include "ui.h"

#include <atomic>
#include <cmath>
#include "runtime.h"

namespace {

constexpr uint32_t kStateMs = 30000;

WiFiClient tcp;
PubSubClient mqtt(tcp);
std::atomic<bool> brokerUp{false};  // for other tasks: the client itself is not shared
String base;  // bdlminer/<host name>

struct Sensor {
    const char *key, *name, *unit, *deviceClass, *stateClass;
};
const Sensor kSensors[] = {
    {"hashrate", "Hashrate", "kH/s", nullptr, "measurement"},
    {"temperature", "Temperature", "°C", "temperature", "measurement"},
    {"accepted", "Accepted shares", nullptr, nullptr, "total_increasing"},
    {"rejected", "Rejected shares", nullptr, nullptr, "total_increasing"},
    {"best_session", "Best difficulty (session)", nullptr, nullptr, "measurement"},
    {"best_ever", "Best difficulty (ever)", nullptr, nullptr, "measurement"},
    {"shares32", "32-bit shares", nullptr, nullptr, "total_increasing"},
    {"shares64", "64-bit shares", nullptr, nullptr, "total_increasing"},
    {"blocks", "Blocks found", nullptr, nullptr, "total_increasing"},
    {"uptime", "Uptime", "s", "duration", "measurement"},
    {"status", "Pool status", nullptr, nullptr, nullptr},
};

// Commands from Home Assistant, applied by mqttApply() in the main loop.
std::atomic<int> pendingPower{-1}, pendingBrightness{-1};
std::atomic<bool> pendingRestart{false};
uint8_t resumePower = 100;  // power restored by the "Mining" switch

void deviceInfo(JsonObject dev, const String &id) {
    dev["identifiers"].add(id);
    dev["name"] = "BDLMiner " + id;
    dev["model"] = board::modelName();
    dev["manufacturer"] = "BDLMiner";
    dev["sw_version"] = FW_VERSION;
    dev["configuration_url"] = "http://" + WiFi.localIP().toString() + "/";
}

// Home Assistant controls (MQTT discovery), commanded on <base>/set/<key>.
void announceControls(const String &id) {
    struct Control { const char *component, *key, *name; int maximum; };
    const Control kControls[] = {
        {"switch", "mining", "Mining", 0},
        {"number", "power", "Mining power", 100},
        {"number", "brightness", "Screen brightness", 100},
        {"button", "restart", "Restart", 0},
    };
    for (const Control &c : kControls) {
        JsonDocument d;
        d["name"] = c.name;
        d["unique_id"] = id + "_" + c.key;
        d["command_topic"] = base + "/set/" + c.key;
        d["availability_topic"] = base + "/status";
        if (strcmp(c.component, "button")) {
            d["state_topic"] = base + "/state";
            d["value_template"] = String("{{ value_json.") + (strcmp(c.key, "power") ? c.key : "power_limit") + " }}";
        }
        if (!strcmp(c.component, "number")) {
            d["min"] = 0;
            d["max"] = c.maximum;
            d["step"] = 5;
            d["unit_of_measurement"] = "%";
            d["mode"] = "slider";
        }
        if (!strcmp(c.component, "button")) d["device_class"] = "restart";
        deviceInfo(d["device"].to<JsonObject>(), id);
        String payload;
        serializeJson(d, payload);
        mqtt.publish(("homeassistant/" + String(c.component) + "/" + id + "/" + c.key + "/config").c_str(), payload.c_str(), true);
    }
}

void onCommand(char *topic, uint8_t *payload, unsigned int length) {
    String value;
    value.reserve(length);
    for (unsigned i = 0; i < length; ++i) value += char(payload[i]);
    value.trim();
    String prefix = base + "/set/";
    if (strncmp(topic, prefix.c_str(), prefix.length())) return;
    const char *key = topic + prefix.length();
    long number = value.toInt();
    bool numeric = value.length() && (isdigit((unsigned char)value[0]) || value[0] == '-');
    if (!strcmp(key, "mining")) {
        if (value.equalsIgnoreCase("ON")) pendingPower = resumePower ? resumePower : 100;
        else if (value.equalsIgnoreCase("OFF")) pendingPower = 0;
    } else if (!strcmp(key, "power") && numeric && number >= 0 && number <= 100) {
        pendingPower = int(number);
    } else if (!strcmp(key, "brightness") && numeric && number >= 0 && number <= 100) {
        pendingBrightness = int(number);
    } else if (!strcmp(key, "restart") && value.equalsIgnoreCase("PRESS")) {
        pendingRestart = true;
    } else {
        return;
    }
    logf("[mqtt] command %s = %s\n", key, value.c_str());
}

void announce() {
    const String &id = webHostName();
    announceControls(id);
    for (const Sensor &s : kSensors) {
        JsonDocument d;
        d["name"] = s.name;
        d["unique_id"] = id + "_" + s.key;
        d["state_topic"] = base + "/state";
        // A missing or null value gives an unknown state instead of a template warning.
        d["value_template"] = String("{{ value_json.") + s.key + " | default(none) }}";
        d["availability_topic"] = base + "/status";
        if (s.unit) d["unit_of_measurement"] = s.unit;
        if (s.deviceClass) d["device_class"] = s.deviceClass;
        if (s.stateClass) d["state_class"] = s.stateClass;
        deviceInfo(d["device"].to<JsonObject>(), id);
        String payload;
        serializeJson(d, payload);
        mqtt.publish(("homeassistant/sensor/" + id + "/" + s.key + "/config").c_str(), payload.c_str(), true);
    }
}

void publishState() {
    const MinerStats stats = minerStats();
    JsonDocument d;
    d["hashrate"] = round(minerHashrate() / 10.0) / 100.0;  // kH/s, 2 decimals
    d["power_percent"] = minerPowerPercent();
    // Always present (Home Assistant templates read it): null without a valid reading.
    float t = board::temperature();
    if (std::isfinite(t)) d["temperature"] = round(t * 10) / 10.0;
    else d["temperature"] = nullptr;
    d["accepted"] = stats.accepted;
    d["rejected"] = stats.rejected;
    d["best_session"] = stats.sessionBestDiff;
    d["best_ever"] = stats.bestDiff;
    d["shares32"] = stats.over32bits;
    d["shares64"] = stats.over64bits;
    d["blocks"] = stats.blocksFound;
    d["uptime"] = minerUptimeSeconds();
    d["status"] = netStatus();
    d["power_limit"] = settings.miningPower;
    d["mining"] = settings.miningPower ? "ON" : "OFF";
    d["brightness"] = settingsBrightness();
    String payload;
    serializeJson(d, payload);
    mqtt.publish((base + "/state").c_str(), payload.c_str());
}

void mqttTask(void *) {
    uint32_t lastState = 0, lastTry = 0;
    bool tried = false;  // the first attempt comes at once, then one every 30 s
    for (;;) {
        if (settings.mqttHost.isEmpty() || !WiFi.isConnected()) {
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }
        if (!mqtt.connected() && (!tried || millis() - lastTry > 30000)) {
            tried = true;
            lastTry = millis();
            base = "bdlminer/" + webHostName();
            mqtt.setServer(settings.mqttHost.c_str(), settings.mqttPort);
            mqtt.setBufferSize(1024);
            String will = base + "/status";
            bool ok = settings.mqttUser.isEmpty()
                          ? mqtt.connect(webHostName().c_str(), will.c_str(), 0, true, "offline")
                          : mqtt.connect(webHostName().c_str(), settings.mqttUser.c_str(), settings.mqttPass.c_str(),
                                         will.c_str(), 0, true, "offline");
            if (ok) {
                logf("[mqtt] connected to %s:%u\n", settings.mqttHost.c_str(), settings.mqttPort);
                mqtt.setCallback(onCommand);
                mqtt.subscribe((base + "/set/#").c_str());
                mqtt.publish(will.c_str(), "online", true);
                announce();
                lastState = 0;
            } else {
                logf("[mqtt] cannot connect to %s:%u (state %d)\n", settings.mqttHost.c_str(), settings.mqttPort,
                     mqtt.state());
            }
        }
        brokerUp = mqtt.connected();
        if (mqtt.connected()) {
            mqtt.loop();
            if (!lastState || millis() - lastState > kStateMs) {
                lastState = millis();
                publishState();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

}  // namespace

void mqttStart() {
    if (!settings.mqttHost.isEmpty())
        runtime::started(xTaskCreatePinnedToCore(mqttTask, "mqtt", 6144, nullptr, 1, nullptr, 0) == pdPASS,
                         runtime::Mqtt, "MQTT");
}

void mqttApply() {
    int power = pendingPower.exchange(-1), brightness = pendingBrightness.exchange(-1);
    if (power >= 0 && power != settings.miningPower) {
        if (settings.miningPower) resumePower = settings.miningPower;
        settings.miningPower = uint8_t(power);  // read by the thermal policy each second
        Settings copy = settings;
        copy.brightness = settingsBrightness();
        settingsSave(copy);
        logf("[mqtt] mining power %u%%\n", (unsigned)power);
    }
    if (brightness >= 0) {
        settingsSetBrightness(uint8_t(brightness));
        ui::setBacklight(settingsBrightness());
    }
    if (pendingRestart.exchange(false)) webRequestRestart("restart requested over MQTT");
}

const char *mqttState() {
    if (settings.mqttHost.isEmpty()) return "off";
    return brokerUp ? "connected" : "disconnected";
}
