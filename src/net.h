// Network task: keeps a Stratum connection to the pool, feeds jobs to the
// miners and submits their shares.
#pragma once

#include <Arduino.h>

void netStart();

// Short human readable state for the UI ("Mining", "Connecting"...).
String netStatus();

// Pool currently in use: the main one, or the backup after repeated failures.
const String &netPoolHost();
uint16_t netPoolPort();
bool netOnBackup();
uint8_t netPendingShares();
// Automatically selected identity for the current/latest connection attempt.
const char *netUserAgent();
