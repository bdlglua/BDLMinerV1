// Read-only status dashboard served by the miner on port 80:
//   http://<ip>/  or  http://bdlminer-xxxx.local/   live page
//   /api/stats                                        JSON
//   /metrics                                          Prometheus
#pragma once

#include <Arduino.h>

void webStart();
// Applies scheduled restarts; call from loop(). HTTP runs in its own task so
// a slow upload cannot block sensor sampling, buttons or the display.
void webHandle();
// Schedules the same restart as the dashboard's (saves statistics first).
void webRequestRestart(const char *why);
// mDNS host name ("bdlminer-xxxx"), empty before webStart().
const String &webHostName();
