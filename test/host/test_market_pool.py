#!/usr/bin/env python3
"""Ensure production pool statistics cannot leak across main/backup failover."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
source = (root / 'src/market.cpp').read_text()
getter = source[source.index('PoolStats poolStatsGet()'):source.index('MarketData marketGet()')]
number = source[source.index('double poolNumber('):source.index('// "856K"')] if 'double poolNumber(' in source else ''
rate = source[source.index('double parseRate('):source.index('// GET http(s)://host:port/path')]
discovery = source[source.index('// "eu.pool.example" -> "pool.example"'):source.index('// End of the pool stats parsing.')]
best_assignment = next(line.strip() for line in source.splitlines() if 'p.bestEver =' in line)
prefix = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <ArduinoJson.h>
#include "market.h"
#include <string>
// Arduino's String calls used by parentDomain(), over std::string.
struct HostString : std::string {
 using std::string::string;
 HostString(const std::string &s) : std::string(s) {}
 int indexOf(char c, size_t from = 0) const { size_t i = find(c, from); return i == npos ? -1 : (int)i; }
 HostString substring(size_t from) const { return HostString(substr(from)); }
};
PoolStats pool;
bool poolBackup = false, backup = false;
int dataMux = 0;
bool netOnBackup() { return backup; }
'''
after_rate = r'''
'''
tests = r'''
int main() {
 pool.updatedMs=123;pool.hashrate=999;pool.supported=false;
 auto main=poolStatsGet();assert(main.updatedMs==123 && main.hashrate==999 && !main.supported);
 backup=true;auto missing=poolStatsGet();assert(missing.updatedMs==0 && missing.hashrate==0 && missing.supported);
 poolBackup=true;pool.updatedMs=456;pool.hashrate=10;
 assert(poolStatsGet().hashrate==10);
 backup=false;assert(poolStatsGet().updatedMs==0 && poolStatsGet().hashrate==0);
 for(const char *raw:{R"({"bestDifficulty":12.5})",R"({"bestDifficulty":"12.5"})",R"({"bestDifficulty":" 1.25e1 "})"}) {
  PoolStats p;JsonDocument doc;assert(!deserializeJson(doc,raw));
  BEST_ASSIGNMENT
  assert(p.bestEver==12.5);
 }
 for(const char *raw:{R"({"bestDifficulty":null})",R"({"bestDifficulty":"bad"})",R"({"bestDifficulty":true})",
                     R"({"bestDifficulty":-1})",R"({"bestDifficulty":"12.5oops"})",R"({"bestDifficulty":"nan"})"}) {
  PoolStats p;p.bestEver=9;JsonDocument doc;assert(!deserializeJson(doc,raw));
  BEST_ASSIGNMENT
  assert(p.bestEver==9);
 }
 for(const char *text:{"", "bad", "-1K", "nan", "inf", "1e309", "1e308E", "12oops", "12KK", "12Kgarbage"})
  assert(parseRate(text, 123)==123);
 assert(parseRate(nullptr, 123)==123);
 assert(parseRate("0K", 123)==0);
 assert(parseRate(" 1.25 k ")==1250);
 assert(parseRate("856K")==856000);
 assert(parseRate("1.25M")==1250000);
 assert(parseRate("125")==125);
 assert(parentDomain("eu.findmyblock.xyz")=="findmyblock.xyz" && parentDomain("public-pool.io")=="" && parentDomain("192.168.1.10")=="");
 assert(parentDomain("a.b.pool.example")=="b.pool.example" && parentDomain("localhost")=="");
 {PoolStats p;JsonDocument doc;deserializeJson(doc,R"({"bestDifficulty":"52.1","workersCount":2,"workers":[{"hashRate":"500000"},{"hashRate":250000}],"accounting":{"totalAcceptedShares":7}})");
  assert(parseStats(doc,p) && p.bestEver==52.1 && p.workers==2 && p.hashrate==750000 && p.shares==7);}
 {PoolStats p;JsonDocument doc;deserializeJson(doc,R"({"hashrate1hr":"856K","bestever":116.5,"shares":42,"workers":1})");
  assert(parseStats(doc,p) && p.hashrate==856000 && p.bestEver==116.5 && p.shares==42 && p.workers==1);}
 {PoolStats p;p.hashrate=5;JsonDocument doc;deserializeJson(doc,R"({"status":"ok","user":"x"})");
  assert(!parseStats(doc,p) && p.hashrate==5);}  // a website's own JSON is not taken for statistics
 puts("Production pool stats discovery: parent domains, public-pool and ckpool answers, foreign JSON refused");
 puts("Production pool stats: main/backup isolation, numeric and text values, invalid-response preservation passed");
}
'''
with tempfile.TemporaryDirectory(prefix='bdl-pool-view-') as temp:
 cpp=Path(temp)/'test.cpp';exe=Path(temp)/'test'
 cpp.write_text(prefix+number+rate+discovery.replace('String','HostString')+getter+tests.replace('BEST_ASSIGNMENT',best_assignment))
 subprocess.run(['g++','-std=c++17','-O2','-Itest/host/stubs/miner','-Isrc','-I.pio/libdeps/esp32-headless/ArduinoJson/src',str(cpp),'-o',str(exe)],cwd=root,check=True)
 subprocess.run([str(exe)],check=True)
