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
size_t heap_caps_get_total_size(int) { return 0; }
size_t heap_caps_get_largest_free_block(int) { return 0; }
void mbedtls_platform_set_calloc_free(void *(*)(size_t,size_t),void(*)(void*)) {}
void disableCore0WDT() { ++pauses; }
void enableCore0WDT() { --pauses; }
namespace hwsha { void hold(){++holds;} void release(){--holds;} }
uint32_t millis() { return 100; }
void logf(const char *,...) {}
struct { unsigned getFreeHeap(){return 1;} unsigned getMaxAllocHeap(){return 1;} } ESP;
const uint8_t kTlsRootBundle[4]={0,1,0,0};
bool clockSet=true; bool tlsClockSet(){return clockSet;}
int bundleLocked=-1;  // the global bundle table was set with the lock held
struct WiFiClientSecure {
 bool succeeds=true,bundleTrusted=true;  // bundleTrusted: the server chains to a bundled root
 bool insecure=false,bundle=false;const char *ca=nullptr;int error=0,stops=0;
 void setInsecure() {insecure=true;ca=nullptr;} void setCACert(const char *root){ca=root;insecure=false;}
 void setCACertBundle(const uint8_t *b){bundle=b==kTlsRootBundle;bundleLocked=locks;}
 void setHandshakeTimeout(int) {} void setTimeout(int) {} void stop(){++stops;}
 bool connect(const char *,uint16_t) {
  ++connects;error=0;
  if(!succeeds){error=-0x4c;return false;}  // a network failure (MBEDTLS_ERR_NET_CONNECT_FAILED)
  if(insecure)return true;
  if(ca)return !strcmp(ca,"valid-root")||(error=-0x2700,false);
  if(bundle)return bundleTrusted||(error=-0x3000,false);
  return false;
 }
 int lastError(char *out,size_t n) { if(n)*out=0; return error; }
};
constexpr uint32_t kTlsMinFree=54*1024,kTlsMinBlock=24*1024;
bool tlsConnect(WiFiClientSecure &tls,const char *host,uint16_t port=443,uint8_t timeoutSeconds=12);
bool tlsConnectVerified(WiFiClientSecure &tls,const char *host,const char *root,uint16_t port=443,uint8_t timeoutSeconds=12);
bool tlsConnectSignedContent(WiFiClientSecure &tls,const char *host,uint16_t port=443,uint8_t timeoutSeconds=12);
'''
tests=r'''
int main() {
 if(FAIL_ALLOC) {
  WiFiClientSecure tls; assert(!tlsConnect(tls,"example",443));
  assert(connects==0 && holds==0 && pauses==0);
 } else {
  assert(locks==-1); // initialization made the binary semaphore available
  WiFiClientSecure tls; assert(tlsConnect(tls,"example",443));
  assert(tls.bundle&&!tls.insecure&&bundleLocked==0); // verified against the bundle, set under the lock
  tls.succeeds=false; assert(!tlsConnect(tls,"example",443)&&!tlsCertificateRejected());
  assert(connects==2 && holds==0 && pauses==0 && locks==-1);
  assert(tlsProfile().handshakes==2 && tlsProfile().failures==1);
  tls.succeeds=true;assert(tlsConnectVerified(tls,"example","valid-root")&&!tls.insecure);
  assert(!tlsConnectVerified(tls,"example","wrong-root")&&!tls.insecure);
  assert(!tlsConnectVerified(tls,"example",nullptr)&&connects==4);
  assert(holds==0&&pauses==0&&locks==-1);
  // A server outside the bundle: refused, never retried without verification.
  {WiFiClientSecure untrusted;untrusted.bundleTrusted=false;
   assert(!tlsConnect(untrusted,"selfsigned.local")&&tlsCertificateRejected()&&!untrusted.insecure&&connects==5);}
  // Signed content (firmware) falls back once, only for a certificate failure.
  {WiFiClientSecure github;github.bundleTrusted=false;
   assert(tlsConnectSignedContent(github,"github.com")&&github.insecure&&github.stops==1&&connects==7);}
  {WiFiClientSecure offline;offline.succeeds=false;
   assert(!tlsConnectSignedContent(offline,"github.com")&&!offline.insecure&&connects==8);}
  // Before NTP no certificate can be checked: no handshake at all.
  clockSet=false;
  {WiFiClientSecure early;assert(!tlsConnect(early,"ntfy.sh")&&tlsCertificateRejected()&&connects==8);}
  {WiFiClientSecure early;assert(!tlsConnectVerified(early,"mempool.space","valid-root")&&connects==8);}
  {WiFiClientSecure early;assert(tlsConnectSignedContent(early,"github.com")&&early.insecure&&connects==9);}
  clockSet=true;
  assert(holds==0&&pauses==0&&locks==-1);
  failTake=true; assert(!tlsConnect(tls,"example",443));assert(connects==9 && holds==0 && pauses==0);
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
