// MQTT for Home Assistant: the miner announces its sensors (MQTT discovery)
// and publishes their state every 30 s. Off while settings.mqttHost is empty.
#pragma once

void mqttStart();
