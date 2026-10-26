// Firmware updates from the GitHub releases of GITHUB_REPO. The latest
// release is checked 30 seconds after boot and then every hour (5
// minutes after a failed check), or on request from the dashboard; its
// <FW_ENV>-firmware.bin is installed (by itself with settings.githubUpdate
// == 2, or from the dashboard) only when <FW_ENV>-firmware.bin.sig is a valid
// signature of it by the release key (update_key.h).
#pragma once

#include <stdint.h>

struct GithubUpdate {
    bool checked = false;    // a release was found at least once
    bool checking = false;   // check queued or in progress
    bool available = false;  // newer than this firmware, with its files for this board
    bool installing = false;
    uint8_t progress = 0;    // percent, while installing
    uint32_t installRequest = 0;  // acknowledges an accepted dashboard command
    char version[24] = "";   // of the latest release
    char error[80] = "";     // last problem, "" when none
};

// Checks or installs when it is time. Called every few seconds by the market
// task, which makes the other HTTPS requests (no task and stack of its own:
// boards without PSRAM need that RAM for TLS).
void ghUpdatePoll();
GithubUpdate ghUpdateState();
bool ghUpdateCheck();  // queue an immediate check; false when disabled or busy

// Asks for the available update to be installed (within seconds, by the
// next ghUpdatePoll()); false when there is none.
bool ghUpdateInstall();
