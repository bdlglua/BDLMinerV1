#!/usr/bin/env python3
"""ASIC proof -> production V1 session -> independent loopback Stratum server.

Only 127.0.0.1 is contacted. The server independently reconstructs and hashes
the genesis header, then scripts acceptance, rejection, disconnect and clean work.
"""
from pathlib import Path
import hashlib
import json
import socket
import struct
import subprocess
import threading
import time

root = Path(__file__).resolve().parents[2]
build = root / '.pio/host/block-e2e'
build.mkdir(parents=True, exist_ok=True)
net = (root / 'src/net.cpp').read_text()
session = net[net.index('constexpr uint32_t kPoolSilenceTimeoutMs'):net.index('void netTask(')]
driver = r'''
#define BOARD_GAMMA601 1
#define BOARD_ASIC 1
#define CONFIG_IDF_TARGET_ESP32S3 1
#include "miner.cpp"
#include "settings.h"
#include "net.h"
#include "core/line_buffer.h"
#include "core/pool_agent.h"
#include <WiFi.h>
#include <cassert>
#include <arpa/inet.h>
#include <cstdlib>
Settings settings;
void logf(const char *, ...) {}
const String &netPoolHost() { return settings.poolHost; }
uint16_t netPoolPort() { return settings.poolPort; }
namespace production_session {
SESSION_SOURCE
}
AsicWork original{};
bool injected = false, checkedStale = false;
void uartResult() {
    if (!injected && minerAsicReady()) {
        injected = true; assert(minerAsicWork(original));
        uint8_t genesis[80];
        assert(work::fromHex("0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a29ab5f49ffff001d1dac2b7c",genesis,80));
        assert(!memcmp(original.header,genesis,76));
        QueuedShare ordinary{}; ordinary.generation = original.generation;
        strcpy(ordinary.share.jobId,"obsolete-ordinary");
        for (int i = 0; i < 16; ++i) assert(xQueueSend(shareQueue,&ordinary,0));
        minerAsicCandidate(original,2083236893,0);
        minerAsicCandidate(original,2083236893,0);
        assert(stats.blocksFound == 1 && blockQueueCount == 1 && shareQueue->entries.size() == 16);
    } else if (injected && !checkedStale && !minerAsicCurrent(original.generation)) {
        checkedStale = true; minerAsicCandidate(original,2083236893,0); assert(stats.blocksFound == 1);
    }
}
int main(int argc,char **argv) {
    assert(argc == 4); int port = atoi(argv[1]); assert(port > 0 && port <= 65535);
    int fd = socket(AF_INET,SOCK_STREAM,0); assert(fd >= 0);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(port);
    assert(inet_pton(AF_INET,"127.0.0.1",&address.sin_addr) == 1);
    assert(connect(fd,(sockaddr *)&address,sizeof(address)) == 0);
    jobMutex = xSemaphoreCreateMutex(); shareQueue = xQueueCreate(16,sizeof(QueuedShare));
    settings.poolHost = "127.0.0.1"; settings.poolPort = port; settings.suggestedDifficulty = 1;
    stratum::Protocol protocol; protocol.setCredentials("offline.worker","x");
    stratum::PoolAgent agent; WiFiClient client(fd); readHook = uartResult;
    production_session::session(client,protocol,5000,agent);
    minerInvalidate(); minerPoolConnected(false); close(fd);
    BlockCandidate candidate{}; assert(minerLastBlockCandidate(candidate));
    assert(injected && stats.blocksFound == 1 && stats.sharesFound == 1);
    if (!strcmp(argv[3],"clean-accept")) assert(checkedStale);
    assert(candidate.state == atoi(argv[2]));
    if (candidate.state == BlockShareAccepted) assert(stats.accepted == 1 && stats.acceptedWork == 1);
    if (candidate.state == BlockShareRejected) assert(stats.rejected == 1 && stats.accepted == 0);
    statsLoaded = true; assert(minerSaveStats());
    candidateCount = candidateHead = blockQueueCount = 0; havePreviousCandidate = false;
    minerLoadStats(); assert(minerLastBlockCandidate(candidate) && candidate.previousSession);
    assert(candidate.state == atoi(argv[2])); stratum::Share share{}; assert(!minerNextShare(share,0));
    puts("proof delivered, pool state correlated and saved without replay");
}
'''.replace('SESSION_SOURCE', session)
(build / 'driver.cpp').write_text(driver)
subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wno-unused-variable','-ffunction-sections','-fdata-sections',
                '-Itest/host/stubs/block_e2e','-Itest/host/stubs/miner','-Isrc',
                '-I.pio/libdeps/esp32-headless/ArduinoJson/src',str(build/'driver.cpp'),
                'src/core/sha256.cpp','src/core/work.cpp','src/core/stratum.cpp',
                '-Wl,--gc-sections','-pthread','-o',str(build/'driver')],cwd=root,check=True)

def double_hash(data):
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()

coinbase = bytes.fromhex('0100000001' + '00'*32 + 'ffffffff4d04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73ffffffff0100f2052a01000000434104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac00000000')
assert double_hash(coinbase).hex() == '3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a'
for mode, expected in [('accept',2),('reject',3),('disconnect',4),('clean-accept',2)]:
    listener = socket.socket()
    listener.bind(('127.0.0.1',0)); listener.listen(1); listener.settimeout(10)
    errors = []
    submitted = []
    def pool():
        try:
            with listener.accept()[0] as connection:
                connection.settimeout(10)
                stream = connection.makefile('rb')
                def send(value):
                    connection.sendall((json.dumps(value)+'\n').encode())
                def job(identifier):
                    return {'id':None,'method':'mining.notify','params':[identifier,'00'*32,coinbase[:5].hex(),coinbase[13:].hex(),[], '00000001','1d00ffff','495fab29',True]}
                while line := stream.readline():
                    message = json.loads(line)
                    method, request_id = message['method'], message['id']
                    if method == 'mining.configure':
                        send({'id':request_id,'result':{'version-rolling':False},'error':None})
                    elif method == 'mining.subscribe':
                        send({'id':request_id,'result':[[],'00000000',4],'error':None})
                    elif method == 'mining.suggest_difficulty':
                        send({'id':request_id,'result':True,'error':None})
                    elif method == 'mining.authorize':
                        send({'id':request_id,'result':True,'error':None})
                        send({'id':None,'method':'mining.set_difficulty','params':[1]})
                        send(job('genesis'))
                    elif method == 'mining.submit':
                        params = message['params']; assert params[1] == 'genesis'
                        tx = coinbase[:5] + bytes(4) + bytes.fromhex(params[2]) + coinbase[13:]
                        header = struct.pack('<I',1) + bytes(32) + double_hash(tx) + struct.pack('<III',int(params[3],16),0x1d00ffff,int(params[4],16))
                        proof = double_hash(header)
                        assert proof[::-1].hex() == '000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f'
                        assert int.from_bytes(proof,'little') <= 0xffff << 208
                        submitted.append(params)
                        if mode == 'disconnect':
                            return
                        if mode == 'clean-accept':
                            send(job('next-height'))
                        send({'id':request_id,'result':mode != 'reject','error':[23,'synthetic test rejection',None] if mode == 'reject' else None})
                        time.sleep(0.03)
                        return
        except BaseException as error:
            errors.append(error)
    thread = threading.Thread(target=pool,daemon=True); thread.start()
    try:
        result = subprocess.run([str(build/'driver'),str(listener.getsockname()[1]),str(expected),mode],cwd=root,capture_output=True,text=True,timeout=15)
        thread.join(timeout=2)
        assert not errors, errors
        assert result.returncode == 0, (mode,result.stdout,result.stderr)
        assert len(submitted) == 1, (mode,submitted)
        print('Local Stratum block pipeline:',mode,'passed')
    finally:
        listener.close()
print('ASIC -> real work/header/SHA-256d -> priority queue -> production V1 session -> loopback pool -> matched ACK/disconnect -> NVS reboot: passed')
