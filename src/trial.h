// Trial period of a newly installed firmware (GitHub or web update).
//
// The bootloader keeps the previous firmware until the new one is marked
// valid. It is marked valid once the miner has worked: 3 minutes up,
// connected to the pool and hashing. A crash or restart before that, or 20
// minutes without that proof while the WiFi works, boots the previous firmware
// again. A power cut can do the same, so a version gets a second try; rolled
// back twice, it is no longer installed automatically.
#pragma once

void trialBegin();   // at boot, once notifications are started
void trialPoll();    // once a second from the main loop
// Deliberate restarts (settings, backup, restart button, factory reset, a
// newer update) show the firmware works: confirm it first.
void trialConfirm(const char *why);
bool trialPending();
// Remembers the version a GitHub update is about to boot.
void trialExpect(const char *version);
