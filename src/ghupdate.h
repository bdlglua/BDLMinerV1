// Firmware updates from the GitHub releases of GITHUB_REPO. The latest
// release is checked 2 minutes after boot and then every 12 hours; its
// <FW_ENV>-firmware.bin is installed (by itself with settings.githubUpdate
// == 2, or from the dashboard) only when <FW_ENV>-firmware.bin.sig is a valid
// signature of it by the release key (update_key.h).
#pragma once

#include <stdint.h>

struct GithubUpdate {
    bool checked = false;    // a release was found at least once
    bool available = false;  // newer than this firmware, with its files for this board
    bool installing = false;
    uint8_t progress = 0;    // percent, while installing
    char version[24] = "";   // of the latest release
    char error[80] = "";     // last problem, "" when none
};

void ghUpdateStart();
GithubUpdate ghUpdateState();

// Asks for the available update to be installed; false when there is none.
bool ghUpdateInstall();
