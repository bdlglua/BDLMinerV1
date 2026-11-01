#!/usr/bin/env python3
"""Read-only readiness report for configured miners. Does not install or change settings."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import tempfile
from bdlhost.client import Client, DeviceError, finite, load_config
from hardware_suite import healthy
from release_version import Version


def collect(devices, expected=None):
    if expected: expected=Version(expected).text
    def one(device):
        try:
            sample=Client(device).stats()
            correct=expected is None or sample['firmware']==expected
            power=sample.get('power_percent',100)
            checks={'firmware':correct,'health':healthy(sample),'confirmed':not sample.get('firmware_trial'),
                    'pool_connected':sample.get('pool',{}).get('connected') is True,
                    'mining':sample['hashrate']>0 and finite(power) and power>0}
            status='ready' if all(checks.values()) else 'review'
            return device.id,{'status':status,'reachable':True,'version_matches':correct,
                              'healthy':healthy(sample),'checks':checks,'sample':sample}
        except DeviceError as error:
            return device.id,{'status':'unreachable','reachable':False,'error':str(error)}
    with ThreadPoolExecutor(max_workers=min(len(devices),4)) as executor:
        miners=dict(executor.map(one,devices))
    return {'kind':'fleet-readiness','recorded_utc':datetime.now(timezone.utc).isoformat(),
            'simulated':False,'read_only':True,'expected_firmware':expected,'miners':miners,
            'status':'ready' if all(m['status']=='ready' for m in miners.values()) else 'review'}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config',type=Path,required=True)
    p.add_argument('--expected')
    p.add_argument('--output',type=Path,default=Path('bdlminer-readiness.json'))
    args=p.parse_args()
    try:
        _,devices=load_config(args.config)
        report=collect(devices,args.expected)
    except (ValueError,OSError) as error:
        p.error(str(error))
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.NamedTemporaryFile(mode='w',dir=args.output.parent,delete=False) as tmp:
        json.dump(report,tmp,indent=2,allow_nan=False);tmp.write('\n')
    os.replace(tmp.name,args.output)
    for key,result in report['miners'].items():
        sample=result.get('sample',{})
        print(f"{key}: {result['status']}"+(f" · {sample['firmware']} · {sample['hashrate']:.0f} H/s" if sample else ''))
    print('Read-only report: '+str(args.output))
    raise SystemExit(0 if report['status']=='ready' else 2)


if __name__=='__main__':main()
