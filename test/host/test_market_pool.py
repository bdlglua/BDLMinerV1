#!/usr/bin/env python3
"""Ensure production pool statistics cannot leak across main/backup failover."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
source = (root / 'src/market.cpp').read_text()
getter = source[source.index('PoolStats poolStatsGet()'):source.index('MarketData marketGet()')]
prefix = r'''
#include <cassert>
#include <cstdio>
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
 puts("Production pool stats: main/backup results isolated, obsolete responses hidden on failover");
}
'''
with tempfile.TemporaryDirectory(prefix='bdl-pool-view-') as temp:
 cpp=Path(temp)/'test.cpp';exe=Path(temp)/'test'
 cpp.write_text(prefix+getter+tests)
 subprocess.run(['g++','-std=c++17','-O2','-Itest/host/stubs/miner','-Isrc',str(cpp),'-o',str(exe)],cwd=root,check=True)
 subprocess.run([str(exe)],check=True)
