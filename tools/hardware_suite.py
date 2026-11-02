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


# Match bm1370::Health::fault() in src/core/bm1370.{h,cpp} while powered:
# OFF, VOUT_OV, IOUT_OC, VIN_UV and TEMP. Other latched/summary bits, including
# CML (0x0002), are reported separately rather than treated as a shutdown fault.
GAMMA_STATUS_FAULT_MASK = 0x007c


def gamma_regulator_fault(status):
    return (type(status) is not int or not 0 <= status <= 0xffff or
            bool(status & GAMMA_STATUS_FAULT_MASK))


def health_issues(sample):
    issues = []
    a = sample.get('asic', {})
    if a.get('model') in ('BM1370', 'BM1397') and (a.get('running') is not True or not finite(sample.get('temp_c'), -39)):
        issues.append('ASIC stopped or temperature unavailable')
    limit = sample.get('max_temp_c')
    if finite(limit, 30) and finite(sample.get('temp_c'), -39) and sample['temp_c'] >= limit:
        issues.append('Configured temperature limit reached')
    if a.get('model') == 'BM1370' and a.get('running'):
        for name, value, low, high in (
                ('ASIC temperature', sample.get('temp_c'), 0, 70),
                ('Regulator temperature', a.get('regulator_temp_c'), 0, 90),
                ('Input voltage', a.get('input_v'), 4.6, 5.5),
                ('Core voltage', a.get('core_v'), 1.0, 1.25)):
            if not finite(value, low) or value >= high:
                issues.append(name + ' unavailable or outside bench limits')
        if not finite(a.get('fan_rpm'), 500):
            issues.append('Fan RPM unavailable or below 500')
        if gamma_regulator_fault(a.get('regulator_status')):
            issues.append('Regulator shutdown fault bits set or status unavailable')
    return issues


def healthy(sample):
    return not health_issues(sample)


def counter_measure(first, last):
    if (not first.get('boot_id') or first.get('boot_id') != last.get('boot_id') or
            first.get('firmware') != last.get('firmware') or last['uptime_s'] <= first['uptime_s'] or
            last['total_hashes'] < first['total_hashes']):
        raise ValueError('Reboot, reset counters or insufficient elapsed device time')
    seconds = last['uptime_s'] - first['uptime_s']
    return {'device_seconds': seconds, 'hashrate_hs': (last['total_hashes'] - first['total_hashes']) / seconds}


def measure_counters(client, seconds):
    # A failed hour-long check must retain the offending reading. A transport
    # failure keeps the last successful sample without claiming it was unhealthy.
    report = {'status': 'failed', 'requested_seconds': seconds, 'samples': 0,
              'first': None, 'last': None}
    def remember(sample):
        report.update(last=sample, samples=report['samples'] + 1)
        asic = sample.get('asic', {})
        status = asic.get('regulator_status')
        if asic.get('model') == 'BM1370' and type(status) is int and 0 <= status <= 0xffff:
            warning_bits = status & ~GAMMA_STATUS_FAULT_MASK
            if warning_bits:
                report['regulator_warning_bits'] = report.get('regulator_warning_bits', 0) | warning_bits
                report['warnings'] = ['Gamma regulator STATUS_WORD contains non-trip bits; inspect regulator_warning_bits']
    started = time.monotonic()
    stage = 'initial_read'
    try:
        first = client.stats()
        report['first'] = first
        remember(first)
        stage = 'initial_health'
        issues = health_issues(first)
        if not first.get('pool', {}).get('connected'):
            issues.append('Pool disconnected')
        if first['hashrate'] <= 0:
            issues.append('Mining hashrate is zero')
        if issues:
            report['issues'] = issues
            raise ValueError('Establish healthy active mining before measuring the hardware')
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            time.sleep(5)
            stage = 'sample_read'
            last = client.stats()
            remember(last)
            stage = 'counter_check'
            counter_measure(first, last)
            stage = 'sample_health'
            issues = health_issues(last)
            if not last.get('pool', {}).get('connected'):
                issues.append('Pool disconnected')
            if issues:
                report['issues'] = issues
                raise ValueError('Hardware health check failed')
        measured = counter_measure(first, report['last'])
        report.update(measured, status='passed' if measured['hashrate_hs'] > 0 else 'failed')
    except Exception as error:
        report.update(error=str(error) if isinstance(error, (ValueError, DeviceError)) else type(error).__name__,
                      error_type=type(error).__name__, failure_stage=stage)
    report['elapsed_s'] = round(time.monotonic() - started, 3)
    return report


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
    case('counters_and_health', lambda: measure_counters(client, args.seconds))
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
