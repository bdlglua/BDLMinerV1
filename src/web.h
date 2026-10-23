// Read-only status dashboard served by the miner on port 80:
//   http://<ip>/  or  http://nerdminer-xxxx.local/   live page
//   /api/stats                                        JSON
#pragma once

#include <Arduino.h>

void webStart();
// Serves pending requests; call from loop().
void webHandle();
// mDNS host name ("nerdminer-xxxx"), empty before webStart().
const String &webHostName();
