#!/usr/bin/env python3
"""Compare portable SHA-256d with a Git baseline; this is not a board hashrate test."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline',required=True,help='Git commit/ref before the optimization')
    p.add_argument('--iterations',type=int,default=400000)
    p.add_argument('--runs',type=int,default=7)
    p.add_argument('--output',type=Path)
    args=p.parse_args()
    if args.iterations<10000 or not 3<=args.runs<=30:p.error('Use >= 10000 iterations and 3–30 runs')
    ref=subprocess.check_output(['git','rev-parse','--verify','--end-of-options',args.baseline+'^{commit}'],cwd=ROOT,text=True).strip()
    baseline_source=subprocess.check_output(['git','show',ref+':src/core/sha256.cpp'],cwd=ROOT)
    compiler=os.environ.get('CXX','g++')
    readings={name:{64:[],80:[]} for name in ('baseline','current')}
    guards={}
    with tempfile.TemporaryDirectory(prefix='bdl-sha-bench-') as tmp:
        folder=Path(tmp);before=folder/'sha256.cpp'
        before.write_bytes(baseline_source)
        bins={}
        for name,source in (('baseline',before),('current',ROOT/'src/core/sha256.cpp')):
            binary=folder/name;bins[name]=binary
            subprocess.run([compiler,'-std=c++17','-O2','-Isrc','-Isrc/core','test/host/sha256_driver.cpp',str(source),'-o',str(binary)],cwd=ROOT,check=True)
        for run in range(args.runs+1):
            # Warm both executables, then alternate order to reduce thermal/order bias.
            for name in (('baseline','current') if run%2==0 else ('current','baseline')):
                result=subprocess.check_output([str(bins[name]),'bench',str(args.iterations)],text=True)
                for line in result.splitlines():
                    length,count,seconds,guard=line.split();length=int(length)
                    if length in guards and guards[length]!=guard:raise ValueError('Baseline/current proof mismatch')
                    guards[length]=guard
                    if run:readings[name][length].append(int(count)/float(seconds))
    results={}
    for length in (64,80):
        before=statistics.median(readings['baseline'][length]);after=statistics.median(readings['current'][length])
        results[str(length)]={'baseline_hs':before,'current_hs':after,'change_percent':100*(after/before-1),
                              'samples_hs':{name:readings[name][length] for name in readings}}
    report={'kind':'native-sha256d-benchmark','baseline_commit':ref,'runs':args.runs,
            'iterations_per_run':args.iterations,'compiler':compiler,'results':results,
            'compiler_version':subprocess.check_output([compiler,'--version'],text=True).splitlines()[0],
            'host_arch':platform.machine(),'compiler_flags':['-std=c++17','-O2'],
            'baseline_source_sha256':hashlib.sha256(baseline_source).hexdigest(),
            'current_source_sha256':hashlib.sha256((ROOT/'src/core/sha256.cpp').read_bytes()).hexdigest(),
            'scope':'Portable SHA-256d only; does not measure ESP32 hardware mining or ASIC silicon throughput'}
    print(json.dumps(report,indent=2))
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':main()
