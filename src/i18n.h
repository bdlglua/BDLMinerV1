// Screen texts in the language of settings.language (0 English, 1 French).
// French texts are plain ASCII: the display fonts have no accented letters.
#pragma once

#include <Arduino.h>
#include <time.h>

// The French text of an English one (a literal or a printf format), or `en`
// itself in English or when there is no translation.
const char *tr(const char *en);

// A pool status as netStatus() gives it ("Connecting pool.x", "Mining"...).
String trStatus(const char *status);

// "Thu 23 Oct 2026" / "jeu 23 oct 2026".
String trDate(const struct tm &t);
