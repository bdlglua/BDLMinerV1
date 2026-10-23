// Read-only status dashboard served by the miner on port 80:
//   http://<ip>/  or  http://bdlminer-xxxx.local/   live page
//   /api/stats                                        JSON
//   /metrics                                          Prometheus
#pragma once

#include <Arduino.h>

void webStart();
// Serves pending requests; call from loop().
void webHandle();
// mDNS host name ("bdlminer-xxxx"), empty before webStart().
const String &webHostName();
