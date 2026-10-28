#!/usr/bin/env python3
"""Exercise production release discovery and scheduling with deterministic HTTP."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/ghupdate.cpp').read_text()
globals_ = source[source.index('constexpr uint32_t kFirstCheckMs'):source.index('// "1.2.10"')]
versions = source[source.index('// "1.2.10"'):source.index('// GET `url`')]
discovery = source[source.index('// Prefer the small release manifest'):source.index('// 0 when `sig`')]
scheduler = source[source.index('void ghUpdatePoll()'):]
prefix = r'''
#include <ArduinoJson.h>
#include <atomic>
#include <algorithm>
using std::min;
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
#include "ghupdate.h"
#include "ota.h"
#define FW_VERSION "1.0.10"
#define FW_ENV "test"
#define GITHUB_REPO "bdlglua/BDLMinerV1"
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
using portMUX_TYPE = int;
constexpr int portMUX_INITIALIZER_UNLOCKED = 0;
constexpr int HTTP_CODE_OK = 200;
constexpr uint32_t kTlsMinFree = 54 * 1024;
struct String : std::string {
    using std::string::string;
    using std::string::operator=;
    String(const std::string &s) : std::string(s) {}
    String(int n) : std::string(std::to_string(n)) {}
    bool isEmpty() const { return empty(); }
};
size_t copyText(char *out, const char *s, size_t n) {
    size_t size=std::strlen(s);
    if(n) { size_t copied=std::min(size,n-1);std::memcpy(out,s,copied);out[copied]=0; }return size;
}
#define strlcpy copyText
void logf(const char *, ...) {}
void notify(const char *, const char *, int, const char *) {}
uint32_t nowMs=0;
uint32_t millis() { return nowMs; }
struct { bool connected=true; bool isConnected() const { return connected; } } WiFi;
struct { uint32_t heap=100000; uint32_t getFreeHeap() const { return heap; } } ESP;
struct { int githubUpdate=1; bool usageCount=false; } settings;
struct WiFiClientSecure {};
struct HTTPClient {
    std::string body;
    std::istringstream stream;
    bool chunked=false;
    int getSize() const { return chunked?-1:body.size(); }
    std::istringstream &getStream() { stream.str(body); return stream; }
    std::string getString() const { return body; }
    void end() {}
};
struct Response { int code=200; std::string body; bool chunked=false; } manifest, api;
std::vector<std::string> requests;
bool httpsGet(String url, WiFiClientSecure &, HTTPClient &http, int *code) {
    requests.push_back(url);
    const auto &r=url.find("update.json")!=std::string::npos?manifest:api;
    http.body=r.body; http.chunked=r.chunked; *code=r.code; return *code==200;
}
int installs=0, pings=0;
GithubInstallResult installResult=GithubInstallResult::Done;
GithubInstallResult install() { ++installs; return installResult; }
void ping() { ++pings; }
'''
tests = r'''
const char *valid=R"({"tag_name":"v1.0.11","assets":[
 {"name":"other-firmware.bin","browser_download_url":"other"},
 {"name":"test-firmware.bin","browser_download_url":"firmware"},
 {"name":"test-firmware.bin.sig","browser_download_url":"signature"}]})";
void reset() {
    state=GithubUpdate(); firmwareUrl=""; signatureUrl="";
    installRequested=false; checkRequested=false; retryInstall=false; installFailures=0; installRetryAt=0; installResult=GithubInstallResult::Done; nextCheck=kFirstCheckMs; nextPing=kFirstPingMs;
    refusedVersion[0]=refusedError[0]=0;
    nowMs=0; settings.githubUpdate=1; settings.usageCount=false; WiFi.connected=true; ESP.heap=100000;
    installs=pings=0; requests.clear(); manifest={200,valid,false}; api={403,"",false};
    ota::release(ota::Owner::Web); ota::release(ota::Owner::Github);
}
int main() {
    char version[24];
    assert(findVersion("BDLMiner v1.0.11",version,sizeof(version)) && !strcmp(version,"1.0.11"));
    assert(compareVersions("1.0.11","1.0.10")>0 && compareVersions("1.0","1.0.0")==0);

    reset(); assert(check());
    assert(state.checked && state.available && requests.size()==1);
    assert(firmwareUrl=="firmware" && signatureUrl=="signature" && state.error[0]==0);
    reset(); manifest.chunked=true; assert(check() && state.available);
    reset(); manifest={404,"",false}; api={200,valid,false}; assert(check());
    assert(requests.size()==2 && state.available);
    reset(); manifest={200,"{invalid",false}; api={200,valid,false}; assert(check() && requests.size()==2);
    reset(); manifest={404,"",false}; assert(!check());
    assert(strstr(state.error,"HTTP 404") && strstr(state.error,"HTTP 403"));
    reset(); manifest.body=R"({"tag_name":"v1.0.12","assets":[]})"; assert(!check());
    assert(!state.available && strstr(state.error,"no signed firmware"));
    reset(); manifest.body=R"({"tag_name":"v1.0.10","assets":[]})"; assert(check());
    assert(state.checked && !state.available && !state.error[0]);

    reset(); ghUpdatePoll(); assert(requests.empty());
    nowMs=kFirstCheckMs; ghUpdatePoll(); assert(state.checked && requests.size()==1 && installs==0);
    assert(nextCheck==nowMs+kCheckMs);
    assert(ghUpdateCheck() && !ghUpdateCheck() && ghUpdateState().checking);
    ghUpdatePoll(); assert(requests.size()==2 && !ghUpdateState().checking);
    assert(ghUpdateInstall() && !ghUpdateInstall() && ghUpdateState().installing);
    assert(ghUpdateState().installRequest==1);
    ghUpdatePoll(); assert(installs==1 && requests.size()==2 && !installRequested);

    reset(); settings.githubUpdate=2; nowMs=kFirstCheckMs; ghUpdatePoll(); assert(installs==1);
    reset(); settings.githubUpdate=0; nowMs=kFirstCheckMs; ghUpdatePoll();
    assert(requests.empty() && !ghUpdateCheck() && !ghUpdateInstall());
    reset(); assert(ghUpdateCheck()); WiFi.connected=false; ghUpdatePoll();
    assert(requests.empty() && checkRequested && strstr(state.error,"no WiFi"));
    WiFi.connected=true; ESP.heap=1000; ghUpdatePoll(); assert(strstr(state.error,"low memory"));
    ESP.heap=100000; ghUpdatePoll(); assert(state.checked && !checkRequested && state.error[0]==0);
    reset(); ota::acquire(ota::Owner::Web); nowMs=kFirstCheckMs; ghUpdatePoll();
    assert(requests.empty() && !ghUpdateCheck() && !ghUpdateInstall()); ota::release(ota::Owner::Web);
    ghUpdatePoll(); assert(state.checked);
    reset(); manifest.code=404; nowMs=kFirstCheckMs; ghUpdatePoll();
    assert(nextCheck==nowMs+kRetryMs && !state.checked);
    manifest={200,valid,false}; nowMs=nextCheck; ghUpdatePoll(); assert(state.checked);
    reset(); settings.usageCount=true; nowMs=kFirstPingMs; ghUpdatePoll();
    assert(state.checked && pings==0); ghUpdatePoll(); assert(pings==1);
    reset(); settings.githubUpdate=2; installResult=GithubInstallResult::Retry;
    nowMs=kFirstCheckMs; ghUpdatePoll(); assert(installs==1 && retryInstall);
    assert(nextCheck==nowMs+kRetryMs && ghUpdateState().retrySeconds==300);
    nowMs=nextCheck-1; ghUpdatePoll(); assert(installs==1);
    nowMs=nextCheck; ghUpdatePoll(); assert(installs==2 && ghUpdateState().retrySeconds==600);
    installResult=GithubInstallResult::Done; nowMs=nextCheck; ghUpdatePoll();
    assert(installs==3 && !retryInstall && ghUpdateState().retrySeconds==0);
    reset(); assert(check()); installResult=GithubInstallResult::Retry;
    assert(ghUpdateInstall()); ghUpdatePoll(); assert(retryInstall);
    nowMs=nextCheck; ghUpdatePoll(); assert(installs==2); // manual install is retried even in offer-only mode
    reset(); settings.githubUpdate=2; installResult=GithubInstallResult::Refused;
    nowMs=kFirstCheckMs; ghUpdatePoll(); assert(installs==1 && !retryInstall && nextCheck==nowMs+kCheckMs);
    nowMs=nextCheck; ghUpdatePoll(); assert(installs==1); // a refused release is not reinstalled every hour
    assert(ghUpdateInstall()); ghUpdatePoll(); assert(installs==2); // but can still be installed on request
    reset(); settings.githubUpdate=2; installResult=GithubInstallResult::Retry;
    nowMs=kFirstCheckMs; ghUpdatePoll(); manifest.code=404; api.code=403;
    nowMs=nextCheck; ghUpdatePoll(); assert(installs==1 && retryInstall && ghUpdateState().retrySeconds==300);
    puts("GitHub discovery and scheduling: manifest, API fallback, retries, status and OTA isolation passed");
}
'''
with tempfile.TemporaryDirectory(prefix='bdl-ghupdate-') as temp:
    cpp = Path(temp) / 'test.cpp'
    exe = Path(temp) / 'test'
    cpp.write_text(prefix + globals_ + versions + discovery + scheduler + tests)
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Isrc',
                    '-I.pio/libdeps/esp32-headless/ArduinoJson/src', str(cpp), '-o', str(exe)], cwd=root, check=True)
    subprocess.run([str(exe)], check=True)
