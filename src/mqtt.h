// MQTT for Home Assistant: the miner announces its sensors (MQTT discovery)
// and publishes their state every 30 s. Off while settings.mqttHost is empty.
#pragma once

void mqttStart();
// Applies the commands received from Home Assistant (mining on/off, power,
// brightness, restart); call once a second from loop(), which owns the
// settings, the screen and restarts.
void mqttApply();
// "off", "connected" or "disconnected", for the dashboard.
const char *mqttState();
