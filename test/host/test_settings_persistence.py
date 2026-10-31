#!/usr/bin/env python3
"""Fault-inject the production settings save and HTTP handler, not a copy."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
settings = (root / 'src/settings.cpp').read_text()
web = (root / 'src/web.cpp').read_text()
save = settings[settings.index('bool settingsSave('):settings.index('String portalApName()')]
apply = web[web.index('int applySettings('):web.index('void handleGetSettings()')]
post = web[web.index('void handlePostSettings()'):web.index('// GET /api/backup:')]
prefix = r'''
#include <cassert>
#include <cstdio>
#include <atomic>
#include <map>
#include "settings_store.h"
#include "settings_apply.h"
#include "ota.h"
#include <freertos/semphr.h>
Settings settings;
const SettingInfo kSettings[] = {
 {"pool",SettingType::Text,&settings.poolHost,0,"",""},
 {"addr",SettingType::Text,&settings.btcAddress,0,"",""},
 {"power",SettingType::Number,&settings.miningPower,8,"",""},
 {"hook",SettingType::Secret,&settings.webhookUrl,0,"",""}
};
const size_t kSettingCount=4;
const char kSecretMask[] = "\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022";
struct Preferences {
 static inline std::map<std::string,std::vector<uint8_t>> data;
 static inline bool failOpen=false, failWrite=false, corrupt=false;
 bool begin(const char *,bool) { return !failOpen; }
 void end() {}
 size_t getBytesLength(const char *key) { return data[key].size(); }
 size_t getBytes(const char *key,void *out,size_t size) {
  if(data[key].size()!=size)return 0;
  std::memcpy(out,data[key].data(),size);return size;
 }
 size_t putBytes(const char *key,const void *bytes,size_t size) {
  if(failWrite)return 0;
  auto p=static_cast<const uint8_t*>(bytes);data[key].assign(p,p+size);
  if(corrupt)data[key].back()^=1;
  return size;
 }
};
SemaphoreHandle_t settingsSaveMutex=xSemaphoreCreateMutex();
uint32_t settingsSequence=0;
const char *kNamespace="test";
void logf(const char *,...) {}
uint8_t settingsBrightness() { return 80; }
void settingsSanitize(Settings &) {}
bool auth=true;
bool authorized() { return auth; }
uint32_t restartAt=0;
int restarts=0;
void restartSoon(const char *) { ++restarts;restartAt=1; }
std::atomic<bool> settingsChanged{false};
int brightnessSets=0;
void settingsSetBrightness(uint8_t) { ++brightnessSets; }
struct {
 std::string body=R"({"power":50})",message;
 int status=0;
 std::string arg(const char *) { return body; }
 void send(int code,const char *,const std::string &text) { status=code;message=text; }
} server;
'''
tests = r'''
int main() {
 settings.poolHost="pool.example";settings.btcAddress="address.worker";
 Preferences::failOpen=true;handlePostSettings();
 assert(server.status==500 && restarts==0 && settingsSequence==0 && settings.miningPower==100);
 Preferences::failOpen=false;Preferences::failWrite=true;handlePostSettings();
 assert(server.status==500 && restarts==0 && settingsSequence==0);
 Preferences::failWrite=false;assert(settingsSave(settings,false));
 assert(settingsSequence==1); // failed writes did not block normal saves
 Preferences::corrupt=true;handlePostSettings();
 assert(server.status==500 && restarts==0 && settingsSequence==1 && settings.miningPower==100);
 Settings loaded;uint32_t sequence=0;Preferences p;
 assert(settingsstore::load(p,loaded,sequence) && sequence==1 && loaded.miningPower==100);
 // A number is saved and applied at once, without a restart.
 Preferences::corrupt=false;handlePostSettings();
 assert(server.status==200 && restarts==0 && settingsSequence==2 && settings.miningPower==50 && settingsChanged);
 assert(server.message.find("\"restart\":false")!=std::string::npos);
 assert(settingsstore::load(p,loaded,sequence) && loaded.miningPower==50);
 // Nothing changed: nothing written.
 handlePostSettings();assert(server.status==200 && settingsSequence==2 && server.message.find("\"changed\":0")!=std::string::npos);
 // A secret sent back masked (as the page shows it) is kept as it is.
 settings.webhookUrl="https://hook.example/token";
 server.body=std::string(R"({"hook":")")+kSecretMask+R"(","power":50})";handlePostSettings();
 assert(server.status==200 && settingsSequence==2 && settings.webhookUrl=="https://hook.example/token");
 assert(server.message.find("\"changed\":0")!=std::string::npos);
 // A pool change is saved and restarts; live strings stay until then.
 server.body=R"({"pool":"new.pool","power":75})";handlePostSettings();
 assert(server.status==200 && restarts==1 && settingsSequence==3 && settings.poolHost=="pool.example" && settings.miningPower==50);
 assert(settingsstore::load(p,loaded,sequence) && loaded.poolHost=="new.pool" && loaded.miningPower==75);
 assert(!settingsSave(settings,false)); // brightness cannot overwrite staged settings
 handlePostSettings();assert(server.status==409 && restarts==1);
 puts("Production settings HTTP: failed writes => 500 and nothing applied; numbers live without restart; pool change => one restart");
}
'''
with tempfile.TemporaryDirectory(prefix='bdl-settings-') as temp:
    cpp = Path(temp) / 'test.cpp'
    exe = Path(temp) / 'test'
    cpp.write_text(prefix + save + apply + post + tests)
    subprocess.run(['g++', '-std=c++17', '-O2', '-Itest/host/stubs/miner', '-Isrc',
                    '-I.pio/libdeps/esp32-headless/ArduinoJson/src', str(cpp),
                    'src/core/sv2_key.cpp', 'src/core/work.cpp', 'src/core/sha256.cpp', '-o', str(exe)], cwd=root, check=True)
    subprocess.run([str(exe)], check=True)
