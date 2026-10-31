#!/usr/bin/env python3
"""Compile the production upload callbacks with deterministic HTTP/flash stubs."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/web.cpp').read_text()
auth = source[source.index('bool authorized() {'):source.index('void restartSoon(')]
upload = source[source.index('// POST /update (multipart):'):source.index('// Prometheus text format')]
prefix = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include "ota.h"
struct String : std::string {
    using std::string::string;
    using std::string::operator=;
    String(const std::string &s) : std::string(s) {}
    bool isEmpty() const { return empty(); }
    void toLowerCase() { for (char &c : *this) c = std::tolower((unsigned char)c); }
    bool endsWith(const char *s) const {
        std::string suffix(s);
        return size() >= suffix.size() && compare(size()-suffix.size(), suffix.size(), suffix) == 0;
    }
};
enum { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED, BASIC_AUTH };
constexpr size_t UPDATE_SIZE_UNKNOWN = 0;
struct HTTPUpload {
    int status = UPLOAD_FILE_START;
    String filename = "board-firmware.bin";
    size_t currentSize = 4, totalSize = 4;
    uint8_t buf[4] = {0xe9, 1, 2, 3};
};
struct {
    HTTPUpload file;
    bool auth = true;
    int status = 0;
    std::string message;
    HTTPUpload &upload() { return file; }
    bool authenticate(const char *, const char *) { return auth; }
    void send(int code, const char *, const std::string &text) { status=code; message=text; }
    void requestAuthentication(int, const char *, const char *) { status=401; }
} server;
struct { String webPassword = "password"; } settings;
bool ownPage = true;  // fromOwnPage(): Host and Origin of this miner
bool fromOwnPage() { return ownPage; }
struct Writer {
    bool running = false, failBegin = false, failWrite = false, failEnd = false;
    int begins = 0, ends = 0, aborts = 0;
    size_t bytes = 0;
    bool begin(size_t) { ++begins; running=!failBegin; return running; }
    bool isRunning() const { return running; }
    size_t write(const uint8_t *, size_t n) { if(failWrite)return 0; bytes+=n; return n; }
    bool end(bool) { ++ends; if(failEnd)return false; running=false; return true; }
    void abort() { ++aborts; running=false; }
    const char *errorString() const { return "writer error"; }
} Update;
std::atomic<uint32_t> restartAt{0};
int restarts = 0;
void restartSoon(const char *) { restartAt=1; ++restarts; }
void logf(const char *, ...) {}
void vTaskDelay(int) {}
'''
tests = r'''
void reset() {
    ota::release(ota::Owner::Web);
    ota::release(ota::Owner::Github);
    resetUpload();
    server.file=HTTPUpload(); server.auth=true; server.status=0;
    settings.webPassword="password"; Update=Writer(); restartAt=0; restarts=0; ownPage=true;
}
void event(int status) { server.file.status=status; handleUpload(); }
void complete() { event(UPLOAD_FILE_START); event(UPLOAD_FILE_WRITE); event(UPLOAD_FILE_END); }
int main() {
    reset(); handleUpdateDone();
    assert(server.status==400 && Update.ends==0 && restarts==0);

    reset(); server.auth=false; complete(); handleUpdateDone();
    assert(server.status==401 && Update.begins==0 && !ota::busy());
    server.auth=true; handleUpdateDone(); assert(server.status==400);

    reset(); complete();
    assert(Update.ends==0 && Update.running && ota::owns(ota::Owner::Web));
    handleUpdateDone();
    assert(server.status==200 && Update.ends==1 && restarts==1 && ota::owns(ota::Owner::Web));
    server.auth=false; handleUpdateDone();
    assert(server.status==401 && Update.ends==1 && restarts==1);
    server.auth=true; handleUpdateDone();
    assert(server.status==409 && Update.ends==1 && restarts==1);

    reset(); assert(ota::acquire(ota::Owner::Github)); Update.running=true; Update.bytes=100;
    complete(); handleUpdateDone(); event(UPLOAD_FILE_ABORTED);
    assert(server.status==409 && Update.bytes==100 && Update.ends==0 && Update.aborts==0);
    assert(ota::owns(ota::Owner::Github));

    reset(); complete(); event(UPLOAD_FILE_START); event(UPLOAD_FILE_WRITE);
    event(UPLOAD_FILE_END); event(UPLOAD_FILE_START); event(UPLOAD_FILE_END); handleUpdateDone();
    assert(server.status==400 && Update.begins==1 && Update.ends==0 && Update.aborts==1 && !ota::busy());

    reset(); complete(); event(UPLOAD_FILE_ABORTED);
    assert(Update.ends==0 && Update.aborts==1 && !ota::busy());
    handleUpdateDone(); assert(server.status==400);

    reset(); Update.failBegin=true; event(UPLOAD_FILE_START);
    assert(ota::acquire(ota::Owner::Github)); Update.running=true;
    event(UPLOAD_FILE_WRITE); event(UPLOAD_FILE_END); handleUpdateDone();
    assert(server.status==500 && Update.ends==0 && Update.aborts==0 && ota::owns(ota::Owner::Github));

    reset(); Update.failWrite=true; complete(); handleUpdateDone();
    assert(server.status==500 && Update.ends==0 && !ota::busy());

    reset(); server.file.totalSize=0; complete(); handleUpdateDone();
    assert(server.status==400 && Update.ends==0 && !ota::busy());

    for(const char *name : {"board-factory.bin", "FACTORY.BIN", "firmware.bin.sig", "file.txt"}) {
        reset(); server.file.filename=name; complete(); handleUpdateDone();
        assert(server.status==400 && Update.begins==0 && Update.ends==0 && !ota::busy());
    }
    reset(); event(UPLOAD_FILE_START); event(UPLOAD_FILE_WRITE); handleUpdateDone();
    assert(server.status==500 && Update.ends==0 && Update.aborts==1 && !ota::busy());

    reset(); Update.failEnd=true; complete(); handleUpdateDone();
    assert(server.status==500 && Update.ends==1 && restarts==0 && !ota::busy());

    reset(); complete(); server.auth=false; handleUpdateDone();
    assert(server.status==401 && Update.ends==0 && Update.aborts==1 && !ota::busy());

    // Without a web password (the default) no sign-in is asked.
    reset(); settings.webPassword=""; server.auth=false; complete(); handleUpdateDone();
    assert(server.status==200 && Update.ends==1 && restarts==1);

    // Another site's page: refused before anything is written, password or not.
    for (const char *password : {"", "password"}) {
        reset(); settings.webPassword=password; ownPage=false; complete(); handleUpdateDone();
        assert(server.status==403 && Update.begins==0 && restarts==0 && !ota::busy());
    }
    puts("Production HTTP upload callbacks: authorization, ownership, aborts, multipart and activation passed");
}
'''
work = root.parent / 'work'
work.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='web-upload-', dir=work) as temporary:
    cpp = Path(temporary) / 'test.cpp'
    exe = Path(temporary) / 'test'
    cpp.write_text(prefix + auth + upload + tests)
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-I'+str(root/'src'), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
