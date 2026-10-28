#!/usr/bin/env python3
"""Exercise production restart paths while either OTA entry point is active."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
watch=(root/'src/watch.cpp').read_text()
web=(root/'src/web.cpp').read_text()
restart=watch[watch.index('void restart(const char *why)'):watch.index('bool nightNow()')]
handle=web[web.index('void webHandle()'):web.index('const String &webHostName()')]
schedule=web[web.index('void restartSoon('):web.index('const char *accentCss()')]
prefix=r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include "ota.h"
int rebooted=0,saved=0,delays=0;
bool delayExcludedUpdater=false;
std::atomic<uint32_t> restartAt{0};
std::atomic<const char *> restartWhy{"test"};
uint32_t clockMs=1000;
uint32_t millis() {return clockMs;}
void logf(const char *,...) {}
void minerSaveStats() {++saved;}
void setRestartReason(const char *) {}
void trialConfirm(const char *) {}
void delay(int) {
 ++delays;
 delayExcludedUpdater=!ota::acquire(ota::Owner::Github);
 if(!delayExcludedUpdater)ota::release(ota::Owner::Github);
}
struct { void restart(){++rebooted;} } ESP;
'''
tests=r'''
int main() {
 for(auto owner:{ota::Owner::Web,ota::Owner::Github}) {
  assert(ota::acquire(owner));restart("watchdog");
  assert(rebooted==0 && saved==0 && delays==0 && ota::owns(owner));
  ota::release(owner);
 }
 assert(ota::acquire(ota::Owner::Github));restartAt=500;
 webHandle();assert(rebooted==0 && saved==0 && restartAt==500 && ota::owns(ota::Owner::Github));
 ota::release(ota::Owner::Github);restartAt=0;
 restart("watchdog");assert(rebooted==1 && saved==1 && delayExcludedUpdater);
 // A real ESP.restart() never returns; the test releases its reservation.
 ota::release(ota::Owner::Restart);
 restartAt=500;webHandle();assert(rebooted==2 && saved==2);
 ota::release(ota::Owner::Restart);
 assert(ota::acquire(ota::Owner::Web));webHandle();
 assert(rebooted==3 && saved==3 && ota::owns(ota::Owner::Web));
 ota::release(ota::Owner::Web);
 restartAt=0;clockMs=UINT32_MAX-1499;restartSoon("wrapped deadline");
 assert(restartAt==1);clockMs=0;webHandle();assert(rebooted==3);
 clockMs=1;webHandle();assert(rebooted==4);
 ota::release(ota::Owner::Restart);
 puts("Production restarts: watchdog defers both OTA owners, web waits for GitHub, reboot reservation excludes new updates, completed web OTA can reboot");
}
'''
with tempfile.TemporaryDirectory(prefix='bdl-restarts-') as temp:
 cpp=Path(temp)/'test.cpp';exe=Path(temp)/'test'
 cpp.write_text(prefix+restart+schedule+handle+tests)
 subprocess.run(['g++','-std=c++17','-O2','-Isrc',str(cpp),'-o',str(exe)],cwd=root,check=True)
 subprocess.run([str(exe)],check=True)
