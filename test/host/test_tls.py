#!/usr/bin/env python3
"""Fault-inject production TLS lock initialization and connection cleanup."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'src/tls.cpp').read_text()
body=source[source.index('static SemaphoreHandle_t lock'):]
profile=(root/'src/tls.h').read_text().split('struct TlsProfile')[1].split('TlsProfile tlsProfile')[0]
prefix=r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <limits>
using SemaphoreHandle_t=int*;
int semaphore=0, locks=0, holds=0, pauses=0, connects=0, allocations=0;
bool failTake=false;
constexpr int pdTRUE=1, portMAX_DELAY=0, MALLOC_CAP_SPIRAM=1, MALLOC_CAP_INTERNAL=2, MALLOC_CAP_8BIT=4;
using portMUX_TYPE=int;
constexpr int portMUX_INITIALIZER_UNLOCKED=0;
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
SemaphoreHandle_t xSemaphoreCreateBinary() { return FAIL_ALLOC ? nullptr : &semaphore; }
int xSemaphoreGive(SemaphoreHandle_t s) { assert(s); --locks;return 1; }
int xSemaphoreTake(SemaphoreHandle_t s,unsigned) { assert(s);if(failTake)return 0;++locks;return 1; }
void *heap_caps_calloc(size_t n,size_t size,int) { ++allocations; return calloc(n,size); }
void heap_caps_free(void *p) { free(p); }
size_t heap_caps_get_free_size(int) { return 0; }
void mbedtls_platform_set_calloc_free(void *(*)(size_t,size_t),void(*)(void*)) {}
void disableCore0WDT() { ++pauses; }
void enableCore0WDT() { --pauses; }
namespace hwsha { void hold(){++holds;} void release(){--holds;} }
uint32_t millis() { return 100; }
void logf(const char *,...) {}
struct { unsigned getFreeHeap(){return 1;} unsigned getMaxAllocHeap(){return 1;} } ESP;
struct WiFiClientSecure {
 bool succeeds=true;
 void setInsecure() {} void setHandshakeTimeout(int) {}
 bool connect(const char *,uint16_t) { ++connects;return succeeds; }
 void lastError(char *out,size_t n) { if(n)*out=0; }
};
'''
tests=r'''
int main() {
 if(FAIL_ALLOC) {
  WiFiClientSecure tls; assert(!tlsConnect(tls,"example",443));
  assert(connects==0 && holds==0 && pauses==0);
 } else {
  assert(locks==-1); // initialization made the binary semaphore available
  WiFiClientSecure tls; assert(tlsConnect(tls,"example",443));
  tls.succeeds=false; assert(!tlsConnect(tls,"example",443));
  assert(connects==2 && holds==0 && pauses==0 && locks==-1);
  assert(tlsProfile().handshakes==2 && tlsProfile().failures==1);
  failTake=true; assert(!tlsConnect(tls,"example",443));assert(connects==2 && holds==0 && pauses==0);
  auto p=tlsCalloc(2,4);assert(p);heap_caps_free(p);
  allocations=0;assert(!tlsCalloc(std::numeric_limits<size_t>::max(),2));assert(allocations==0);
 }
 puts("Production TLS: failed lock allocation/acquisition is safe, failed handshakes release resources, oversized allocation refused");
}
'''
with tempfile.TemporaryDirectory(prefix='bdl-tls-') as temp:
 cpp=Path(temp)/'test.cpp';exe=Path(temp)/'test'
 cpp.write_text(prefix+'struct TlsProfile'+profile+body+tests)
 for fail in (1,0):
  subprocess.run(['g++','-std=c++17','-O2','-DFAIL_ALLOC='+str(fail),str(cpp),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
