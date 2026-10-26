#!/usr/bin/env python3
"""Ensure production pool statistics cannot leak across main/backup failover."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
source = (root / 'src/market.cpp').read_text()
getter = source[source.index('PoolStats poolStatsGet()'):source.index('MarketData marketGet()')]
number = source[source.index('double poolNumber('):source.index('// "856K"')] if 'double poolNumber(' in source else ''
best_assignment = next(line.strip() for line in source.splitlines() if 'p.bestEver =' in line)
prefix = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <ArduinoJson.h>
#include "market.h"
PoolStats pool;
bool poolBackup = false, backup = false;
int dataMux = 0;
bool netOnBackup() { return backup; }
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
 puts("Production pool stats: main/backup isolation, numeric and text values, invalid-response preservation passed");
}
'''
with tempfile.TemporaryDirectory(prefix='bdl-pool-view-') as temp:
 cpp=Path(temp)/'test.cpp';exe=Path(temp)/'test'
 cpp.write_text(prefix+number+getter+tests.replace('BEST_ASSIGNMENT',best_assignment))
 subprocess.run(['g++','-std=c++17','-O2','-Itest/host/stubs/miner','-Isrc','-I.pio/libdeps/esp32-headless/ArduinoJson/src',str(cpp),'-o',str(exe)],cwd=root,check=True)
 subprocess.run([str(exe)],check=True)
