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

namespace {

constexpr uint32_t kStateMs = 30000;

WiFiClient tcp;
PubSubClient mqtt(tcp);
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
    {"blocks", "Blocks found", nullptr, nullptr, "total_increasing"},
    {"uptime", "Uptime", "s", "duration", "measurement"},
    {"status", "Pool status", nullptr, nullptr, nullptr},
};

void announce() {
    const String &id = webHostName();
    for (const Sensor &s : kSensors) {
        JsonDocument d;
        d["name"] = s.name;
        d["unique_id"] = id + "_" + s.key;
        d["state_topic"] = base + "/state";
        d["value_template"] = String("{{ value_json.") + s.key + " }}";
        d["availability_topic"] = base + "/status";
        if (s.unit) d["unit_of_measurement"] = s.unit;
        if (s.deviceClass) d["device_class"] = s.deviceClass;
        if (s.stateClass) d["state_class"] = s.stateClass;
        JsonObject dev = d["device"].to<JsonObject>();
        dev["identifiers"].add(id);
        dev["name"] = "BDLMiner " + id;
        dev["model"] = board::modelName();
        dev["manufacturer"] = "BDLMiner";
        dev["sw_version"] = FW_VERSION;
        dev["configuration_url"] = "http://" + WiFi.localIP().toString() + "/";
        String payload;
        serializeJson(d, payload);
        mqtt.publish(("homeassistant/sensor/" + id + "/" + s.key + "/config").c_str(), payload.c_str(), true);
    }
}

void publishState() {
    JsonDocument d;
    d["hashrate"] = round(minerHashrate() / 10.0) / 100.0;  // kH/s, 2 decimals
    float t = board::temperature();
    if (!isnan(t)) d["temperature"] = round(t * 10) / 10.0;
    d["accepted"] = stats.accepted;
    d["rejected"] = stats.rejected;
    d["best_session"] = stats.sessionBestDiff;
    d["best_ever"] = stats.bestDiff;
    d["shares32"] = stats.over32bits;
    d["blocks"] = stats.blocksFound;
    d["uptime"] = (millis() - stats.startMillis) / 1000;
    d["status"] = netStatus();
    String payload;
    serializeJson(d, payload);
    mqtt.publish((base + "/state").c_str(), payload.c_str());
}

void mqttTask(void *) {
    uint32_t lastState = 0, lastTry = 0;
    for (;;) {
        if (settings.mqttHost.isEmpty() || !WiFi.isConnected()) {
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }
        if (!mqtt.connected() && millis() - lastTry > 30000) {
            lastTry = millis();
            base = "bdlminer/" + webHostName();
            mqtt.setServer(settings.mqttHost.c_str(), settings.mqttPort);
            mqtt.setBufferSize(768);
            String will = base + "/status";
            bool ok = settings.mqttUser.isEmpty()
                          ? mqtt.connect(webHostName().c_str(), will.c_str(), 0, true, "offline")
                          : mqtt.connect(webHostName().c_str(), settings.mqttUser.c_str(), settings.mqttPass.c_str(),
                                         will.c_str(), 0, true, "offline");
            if (ok) {
                logf("[mqtt] connected to %s:%u\n", settings.mqttHost.c_str(), settings.mqttPort);
                mqtt.publish(will.c_str(), "online", true);
                announce();
                lastState = 0;
            } else {
                logf("[mqtt] cannot connect to %s:%u (state %d)\n", settings.mqttHost.c_str(), settings.mqttPort,
                     mqtt.state());
            }
        }
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

void mqttStart() { xTaskCreatePinnedToCore(mqttTask, "mqtt", 6144, nullptr, 1, nullptr, 0); }
