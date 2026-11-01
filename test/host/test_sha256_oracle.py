#!/usr/bin/env python3
"""Verify the production SHA paths against Python's independent hashlib backend."""
import hashlib
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='bdl-sha-oracle-') as tmp:
    binary=Path(tmp)/'sha'
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Isrc','test/host/sha256_driver.cpp',
                    'src/core/sha256.cpp','-o',str(binary)],cwd=ROOT,check=True)
    lines=subprocess.check_output([str(binary)],text=True).splitlines()
    for line in lines:
        length,one,two,inplace=line.split()
        length=int(length);seed=0x93ac5f17^length;data=bytearray()
        for _ in range(length):
            seed=(seed*1664525+1013904223)&0xffffffff;data.append(seed>>24)
        first=hashlib.sha256(data).digest();second=hashlib.sha256(first).hexdigest()
        assert one==first.hex() and two==inplace==second,(length,one,two,inplace)
    assert len(lines)==517
print('SHA-256/d: 517 independent hashlib vectors, null/empty input, padding boundaries, unaligned and in-place buffers passed')
