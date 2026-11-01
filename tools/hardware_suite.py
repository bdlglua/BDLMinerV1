#!/usr/bin/env python3
"""LAN bench: real counters, diagnostics, optional signed OTA/reset and network faults.

Active operations run only when their arguments are supplied. Disconnect/restore
and physical-reset commands are operator-provided argument vectors (no shell).
Missing hardware capabilities are reported as skipped, never as passed.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import time
from bdlhost.client import Client, DeviceError, finite, load_config
from firmware_image import inspect_app, verify_signature


def healthy(sample):
    a = sample.get('asic', {})
    if a.get('model') in ('BM1370', 'BM1397') and (a.get('running') is not True or not finite(sample.get('temp_c'), -39)):
        return False
    limit = sample.get('max_temp_c')
    if finite(limit, 30) and finite(sample.get('temp_c'), -39) and sample['temp_c'] >= limit:
        return False
    if a.get('model') == 'BM1370' and a.get('running'):
        for value, low, high in ((sample.get('temp_c'), 0, 70), (a.get('regulator_temp_c'), 0, 90),
                                  (a.get('input_v'), 4.6, 5.5), (a.get('core_v'), 1.0, 1.25)):
            if not finite(value, low) or value >= high:
                return False
        if not finite(a.get('fan_rpm'), 500) or a.get('regulator_status') != 0:
            return False
    return True


def counter_measure(first, last):
    if (not first.get('boot_id') or first.get('boot_id') != last.get('boot_id') or
            first.get('firmware') != last.get('firmware') or last['uptime_s'] <= first['uptime_s'] or
            last['total_hashes'] < first['total_hashes']):
        raise ValueError('Reboot, reset counters or insufficient elapsed device time')
    seconds = last['uptime_s'] - first['uptime_s']
    return {'device_seconds': seconds, 'hashrate_hs': (last['total_hashes'] - first['total_hashes']) / seconds}


def command(text):
    result = subprocess.run(shlex.split(text), stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, timeout=30)
    if result.returncode:
        raise ValueError('Operator hardware command returned a failure')


def fault(client, kind, disconnect, restore, timeout=120):
    result = {'status': 'failed', 'observed_outage': False, 'restored': False}
    baseline = client.stats()
    if not healthy(baseline) or not baseline.get('pool', {}).get('connected') or baseline['hashrate'] <= 0:
        raise ValueError('Establish mining before a network fault test')
    try:
        command(disconnect)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                sample = client.stats()
                if not healthy(sample):
                    raise ValueError('Miner health check failed during network fault')
                if kind == 'pool' and sample.get('pool', {}).get('connected') is False:
                    result['observed_outage'] = True
                    break
            except DeviceError:
                if kind == 'wifi':
                    result['observed_outage'] = True
                    break
            time.sleep(2)
    finally:
        command(restore)
        deadline = time.monotonic() + timeout
        resumed = None
        while time.monotonic() < deadline:
            try:
                sample = client.stats()
                if healthy(sample) and sample.get('pool', {}).get('connected') and sample['hashrate'] > 0:
                    if resumed and sample.get('boot_id') == resumed.get('boot_id') and sample['uptime_s'] - resumed['uptime_s'] >= 5 and sample['total_hashes'] > resumed['total_hashes']:
                        result['restored'] = True
                        break
                    if resumed is None or sample.get('boot_id') != resumed.get('boot_id'):
                        resumed = sample
                else:
                    resumed = None
            except DeviceError:
                resumed = None
            time.sleep(2)
    if result['observed_outage'] and result['restored']:
        result['status'] = 'passed'
    return result


def ota(client, firmware, signature, expected, mode='confirm', reset_command=None, timeout=1500):
    first = client.stats()
    previous = first['firmware']
    if first.get('firmware_trial') or expected == previous:
        raise ValueError('Start from confirmed firmware with a different version')
    verify_signature(firmware, signature)
    binary = firmware.read_bytes()
    inspect_app(binary, first['firmware_env'], expected)
    capabilities = client.settings().get('capabilities', {})
    if capabilities.get('ota') is False:
        raise ValueError('This board requires a USB update')
    if mode == 'rollback' and not reset_command:
        raise ValueError('Rollback requires a physical reset command; web restart confirms the trial')
    boundary = 'BDLBench' + os.urandom(16).hex()
    body = (f'--{boundary}\r\nContent-Disposition: form-data; name="firmware"; filename="firmware.bin"\r\nContent-Type: application/octet-stream\r\n\r\n').encode() + binary + f'\r\n--{boundary}--\r\n'.encode()
    client.request('/update', body, method='POST', raw=True, content_type='multipart/form-data; boundary=' + boundary)
    pending = False
    reset = False
    events = []
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            sample = client.stats()
        except DeviceError:
            time.sleep(2)
            continue
        event = {k: sample.get(k) for k in ('firmware', 'boot_id', 'firmware_trial')}
        if not events or events[-1] != event:
            events.append(event)
        if sample['firmware'] == expected and sample.get('firmware_trial') is True:
            pending = True
            if mode == 'rollback' and not reset:
                command(reset_command)
                reset = True
        desired = previous if mode == 'rollback' else expected
        if pending and sample['firmware'] == desired and sample.get('firmware_trial') is False:
            if sample.get('boot_id') == first.get('boot_id'):
                raise ValueError('OTA did not produce a new boot ID')
            if healthy(sample) and sample.get('pool', {}).get('connected') and sample['hashrate'] > 0:
                return {'status': 'passed', 'saw_pending_trial': True,
                        'observed_result': 'rollback' if mode == 'rollback' else 'confirmed', 'events': events, 'last': sample}
        time.sleep(2)
    return {'status': 'failed', 'error': 'OTA result not observed within timeout',
            'saw_pending_trial': pending, 'events': events}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config', type=Path, required=True)
    p.add_argument('--miner', required=True)
    p.add_argument('--seconds', type=int, default=120)
    p.add_argument('--output', type=Path, default=Path('bdlminer-suite.json'))
    p.add_argument('--firmware', type=Path)
    p.add_argument('--signature', type=Path)
    p.add_argument('--version')
    p.add_argument('--ota-mode', choices=['confirm', 'rollback'], default='confirm')
    p.add_argument('--reset-command')
    p.add_argument('--fault-kind', choices=['pool', 'wifi'])
    p.add_argument('--disconnect-command')
    p.add_argument('--restore-command')
    args = p.parse_args()
    if args.seconds < 60:
        p.error('Measure at least 60 seconds')
    if bool(args.firmware) != bool(args.signature) or bool(args.firmware) != bool(args.version):
        p.error('OTA requires --firmware, --signature and --version together')
    if args.fault_kind and not (args.disconnect_command and args.restore_command):
        p.error('A fault test requires both disconnect and restore commands')
    _, devices = load_config(args.config)
    device = next((d for d in devices if d.id == args.miner), None)
    if device is None:
        p.error('Unknown configured miner')
    client = Client(device, timeout=30 if args.firmware else 8)
    results = {}
    def case(name, fn):
        try:
            results[name] = fn()
        except Exception as error:
            results[name] = {'status': 'failed', 'error': str(error) if isinstance(error, (ValueError, DeviceError)) else type(error).__name__}
        print(name + ': ' + results[name]['status'], flush=True)
    def counters():
        first = client.stats()
        if not healthy(first) or not first.get('pool', {}).get('connected') or first['hashrate'] <= 0:
            raise ValueError('Establish healthy active mining before measuring the hardware')
        last = first
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            time.sleep(5)
            last = client.stats()
            counter_measure(first, last)
            if not healthy(last) or not last.get('pool', {}).get('connected'):
                raise ValueError('Hardware health check failed')
        measured=counter_measure(first,last)
        return {'status': 'passed' if measured['hashrate_hs'] > 0 else 'failed', **measured, 'first': first, 'last': last}
    case('counters_and_health', counters)
    def diagnostics():
        if client.stats().get('api_version', 0) < 2:
            return {'status': 'skipped', 'reason': 'Pool diagnostics require V2 firmware; counter/health checks remain available'}
        report = client.diagnostic()
        return {'status': 'passed' if report.get('compatible') else 'failed', 'report': report}
    case('pool_diagnostics', diagnostics)
    if args.fault_kind:
        case('network_recovery', lambda: fault(client, args.fault_kind, args.disconnect_command, args.restore_command))
    else:
        results['network_recovery'] = {'status': 'skipped', 'reason': 'No operator disconnect/restore hardware commands supplied'}
    if args.firmware:
        case('ota', lambda: ota(client, args.firmware, args.signature, args.version, args.ota_mode, args.reset_command))
    else:
        results['ota'] = {'status': 'skipped', 'reason': 'No signed firmware supplied; no update attempted'}
    results['physical_display'] = {'status': 'skipped', 'reason': 'Requires visual inspection on the physical board'}
    report = {'recorded_utc': datetime.now(timezone.utc).isoformat(), 'kind': 'hardware-suite',
              'simulated': False, 'miner_id': device.id, 'cases': results,
              'status': 'failed' if any(r['status'] == 'failed' for r in results.values()) else 'completed'}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode='w', dir=args.output.parent, delete=False) as output:
        json.dump(report, output, indent=2, allow_nan=False)
    os.replace(output.name, args.output)
    print('Report: ' + str(args.output))
    raise SystemExit(report['status'] == 'failed')


if __name__ == '__main__':
    main()
