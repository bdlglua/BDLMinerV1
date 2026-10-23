// Network task: keeps a Stratum connection to the pool, feeds jobs to the
// miners and submits their shares.
#pragma once

#include <Arduino.h>

void netStart();

// Short human readable state for the UI ("Mining", "Connecting"...).
const char *netStatus();
