#!/usr/bin/env python3
"""Read-only hardware checks to run on the same LAN as the miners.

snapshot: record actual readings. measure: compare dashboard closed/open.
ota-monitor: observe a real pending trial and confirmation/rollback; does not
upload firmware, restart a miner or change settings. Web password, if needed,
comes from BDLMINER_WEB_PASSWORD and is never included in reports.
"""
import argparse
import base64
import concurrent.futures
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import tempfile
import time
import urllib.parse
import urllib.request
import webbrowser


def device_url(value):
    if '://' not in value:
        value = 'http://' + value
    parsed = urllib.parse.urlsplit(value)
    if parsed.scheme not in ('http', 'https') or not parsed.hostname or parsed.username or parsed.password or parsed.query or parsed.fragment:
        raise argparse.ArgumentTypeError('Use a device HTTP(S) URL without credentials or query parameters')
    return value.rstrip('/')


def finite_number(value):
    return not isinstance(value, bool) and isinstance(value, (float, int)) and math.isfinite(value) and value >= 0


def read_stats(url):
    headers = {'Accept': 'application/json', 'User-Agent': 'BDLMiner-hardware-check/1'}
    password = os.environ.get('BDLMINER_WEB_PASSWORD')
    if password:
        headers['Authorization'] = 'Basic ' + base64.b64encode(('admin:' + password).encode()).decode()
    request = urllib.request.Request(url + '/api/stats?history=0', headers=headers)
    # urllib preserves the configured proxy and normal HTTPS CA verification.
    with urllib.request.urlopen(request, timeout=8) as response:
        body = response.read(65537)
    if len(body) > 65536:
        raise ValueError('Statistics response exceeds 64 KiB')
    data = json.loads(body)
    if not isinstance(data, dict) or not isinstance(data.get('firmware'), str) or not data.get('board'):
        raise ValueError('Not a BDLMiner statistics response')
    for key in ('uptime_s', 'total_hashes', 'hashrate'):
        if not finite_number(data.get(key)):
            raise ValueError('Invalid measurement: ' + key)
    # Deliberately omit wallet, SSID, settings/secrets, peers and logs.
    allowed = ('board', 'firmware', 'firmware_env', 'boot_id', 'uptime_s', 'total_hashes',
               'hashrate', 'hashrate_live', 'temp_c', 'temperature_source', 'max_temp_c',
               'power_percent', 'limitation_reason', 'heap', 'heap_max_block', 'firmware_trial', 'shares', 'asic')
    result = {key: data[key] for key in allowed if key in data}
    pool = data.get('pool', {})
    for source, target in (('connected', 'pool_connected'), ('difficulty', 'pool_difficulty'),
                           ('status', 'pool_status'), ('pending_shares', 'pool_pending_shares'),
                           ('protocol', 'pool_protocol')):
        if source in pool:
            result[target] = pool[source]
    return result


def health(sample):
    issues = []
    asic = sample.get('asic', {})
    if asic.get('running') and sample.get('temp_c') is None:
        issues.append('Running ASIC has no usable temperature')
    if asic.get('running') and asic.get('model') == 'BM1370':
        if asic.get('fan_rpm', 0) < 500:
            issues.append('Gamma fan RPM is below the firmware interlock')
        if finite_number(sample.get('temp_c')) and sample['temp_c'] >= 70:
            issues.append('Gamma ASIC is at/above the hard temperature limit')
        if finite_number(asic.get('regulator_temp_c')) and asic['regulator_temp_c'] >= 90:
            issues.append('Gamma regulator is at/above the hard temperature limit')
    return issues


def summarize(samples):
    if len(samples) < 2:
        raise ValueError('At least two actual samples are required')
    first, last = samples[0], samples[-1]
    if not first.get('boot_id'):
        raise ValueError('A device boot ID is required for a reliable measurement')
    previous = first
    for sample in samples:
        if (sample.get('boot_id') != first.get('boot_id') or sample['firmware'] != first['firmware'] or
                sample.get('board') != first.get('board') or sample['uptime_s'] < previous['uptime_s'] or
                sample['total_hashes'] < previous['total_hashes']):
            raise ValueError('Miner restarted or counters reset during measurement')
        previous = sample
    seconds = last['uptime_s'] - first['uptime_s']
    if seconds <= 0:
        raise ValueError('No device uptime elapsed; no rate can be reported')
    temperatures = [s['temp_c'] for s in samples if finite_number(s.get('temp_c'))]
    share_delta = {}
    for key in ('accepted', 'rejected', 'work'):
        before, after = (sample.get('shares', {}).get(key) for sample in (first, last))
        share_delta[key] = after - before if finite_number(before) and finite_number(after) and after >= before else None
    # Context makes it possible to distinguish a CPU/load change from thermal
    # throttling, a pool outage or a change to the requested power setting.
    context = {}
    for key in ('power_percent', 'max_temp_c', 'pool_difficulty'):
        context[key + '_levels'] = sorted({s[key] for s in samples if finite_number(s.get(key))})
    context['limitation_reasons'] = sorted({s['limitation_reason'] for s in samples if isinstance(s.get('limitation_reason'), str)})
    return {'device_seconds': seconds, 'measured_hashrate_hs': (last['total_hashes'] - first['total_hashes']) / seconds,
            'max_temperature_c': max(temperatures) if temperatures else None,
            'pool_connected_samples': sum(s.get('pool_connected') is True for s in samples),
            'samples': len(samples), 'health_issues': sorted({issue for sample in samples for issue in health(sample)}),
            'share_delta': share_delta, **context, 'first': first, 'last': last}


def snapshot(urls):
    def one(url):
        try:
            sample = read_stats(url)
            return {'reachable': True, 'sample': sample, 'health_issues': health(sample)}
        except Exception as error:
            return {'reachable': False, 'error': type(error).__name__ + ': ' + str(error)}
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(len(urls), 4)) as executor:
        return dict(zip(urls, executor.map(one, urls)))


def compare_phases(closed, opened):
    if (closed['first'].get('boot_id') != opened['first'].get('boot_id') or
            closed['first']['firmware'] != opened['first']['firmware']):
        raise ValueError('Miner restarted or firmware changed between dashboard phases')
    baseline = closed['measured_hashrate_hs']
    changed = []
    if any(phase['pool_connected_samples'] != phase['samples'] for phase in (closed, opened)):
        changed.append('Pool connection was not continuous')
    for key, description in (('power_percent_levels', 'Effective mining power changed'),
                             ('max_temp_c_levels', 'Configured temperature limit changed')):
        if closed[key] != opened[key] or any(len(phase[key]) > 1 for phase in (closed, opened)):
            changed.append(description)
    return {'open_vs_closed_percent': (opened['measured_hashrate_hs'] / baseline - 1) * 100 if baseline else None,
            'conditions_to_review': changed}


def measure(args):
    if args.seconds < 120 or args.warmup < 60 or args.interval < 2:
        raise ValueError('Use at least 120 seconds/phase, 60 seconds warmup and 2 seconds between samples')
    phases = {}
    for name in ('dashboard_closed', 'dashboard_open'):
        if name == 'dashboard_closed':
            input('Close all dashboards for these miners, then press Enter: ')
        else:
            for url in args.urls:
                webbrowser.open(url, new=1)
            input('Keep every dashboard in a separate visible window (not background tabs), then press Enter: ')
        print(name, '- warming up for', args.warmup, 'seconds', flush=True)
        time.sleep(args.warmup)
        observations = {url: [] for url in args.urls}
        until = time.monotonic() + args.seconds
        while True:
            for url in args.urls:
                observations[url].append(read_stats(url))
            remaining = until - time.monotonic()
            if remaining <= 0:
                break
            time.sleep(min(args.interval, remaining))
        phases[name] = {url: summarize(samples) for url, samples in observations.items()}
    comparison = {}
    for url in args.urls:
        closed, opened = (phases[name][url] for name in ('dashboard_closed', 'dashboard_open'))
        comparison[url] = compare_phases(closed, opened)
    return {'conditions': 'Operator confirmed closed/open; identical API sampling in both phases',
            'rate_source': 'delta total_hashes / delta device uptime_s', 'phases': phases, 'comparison': comparison}


def ota_result(sample, expected, previous, mode, saw_pending, saw_previous=True):
    if not saw_previous:
        return None
    if sample['firmware'] == expected and sample.get('firmware_trial') is True:
        return 'pending'
    if not saw_pending:
        return None
    if sample['firmware'] == previous:
        return 'rollback' if mode == 'rollback' else 'unexpected_rollback'
    if sample['firmware'] == expected and sample.get('firmware_trial') is False:
        return 'confirmed' if mode == 'confirm' else 'confirmed_before_rollback'
    return None


def monitor(args):
    if args.expected == args.previous:
        raise ValueError('Previous and expected versions must differ')
    print('Monitoring only. Apply the matching firmware through the local dashboard.', flush=True)
    if args.expect == 'rollback':
        print('This requires a controlled physical/serial reset during the unconfirmed trial; this script cannot perform it.', flush=True)
    until = time.monotonic() + args.timeout
    pending = False
    baseline = None
    events = []
    while time.monotonic() < until:
        try:
            sample = read_stats(args.urls[0])
            if baseline is None and sample['firmware'] == args.previous:
                baseline = sample
            if baseline and (sample.get('board') != baseline.get('board') or
                             sample.get('firmware_env') != baseline.get('firmware_env')):
                return {'passed': False, 'observed_result': 'different_device', 'events': events}
            result = ota_result(sample, args.expected, args.previous, args.expect, pending, baseline is not None)
            if result == 'pending':
                pending = True
            if len(events) < 200:
                events.append({'firmware': sample['firmware'], 'boot_id': sample.get('boot_id'), 'trial': sample.get('firmware_trial')})
            if result and result != 'pending':
                return {'observed_result': result, 'saw_pending_trial': pending, 'events': events, 'last': sample,
                        'passed': result in ('confirmed', 'rollback')}
        except Exception as error:
            if len(events) < 200:
                events.append({'offline_or_error': type(error).__name__})
        time.sleep(2)
    return {'passed': False, 'observed_result': 'timeout', 'saw_pending_trial': pending, 'events': events}


def write_report(path, report):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', dir=path.parent, delete=False, encoding='utf-8') as file:
            temporary = file.name
            json.dump(report, file, indent=2, allow_nan=False)
        os.replace(temporary, path)
        temporary = None
    finally:
        if temporary and os.path.exists(temporary):
            os.unlink(temporary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    for command in ('snapshot', 'measure', 'ota-monitor'):
        p = sub.add_parser(command)
        p.add_argument('urls', nargs='+' if command != 'ota-monitor' else 1, type=device_url)
        p.add_argument('--output', type=Path, default=Path('.pio/hardware-report.json'))
        if command == 'measure':
            p.add_argument('--seconds', type=int, default=180)
            p.add_argument('--warmup', type=int, default=60)
            p.add_argument('--interval', type=int, default=10)
        if command == 'ota-monitor':
            p.add_argument('--expected', required=True)
            p.add_argument('--previous', required=True)
            p.add_argument('--expect', choices=('confirm', 'rollback'), required=True)
            p.add_argument('--timeout', type=int, default=600)
    args = parser.parse_args()
    try:
        result = snapshot(args.urls) if args.command == 'snapshot' else measure(args) if args.command == 'measure' else monitor(args)
        passed = not (args.command == 'snapshot' and any(not item['reachable'] or item['health_issues'] for item in result.values()))
        if args.command == 'ota-monitor':
            passed = result['passed']
        report = {'recorded_utc': datetime.now(timezone.utc).isoformat(), 'tool_version': 2, 'kind': args.command,
                  'status': 'completed' if passed else 'failed', 'simulated': False, 'result': result}
        write_report(args.output, report)
        print('Report:', args.output)
        return 0 if passed else 1
    except (Exception, KeyboardInterrupt) as error:
        try:
            write_report(args.output, {'recorded_utc': datetime.now(timezone.utc).isoformat(), 'kind': args.command,
                                      'status': 'failed', 'simulated': False, 'error': type(error).__name__ + ': ' + str(error)})
        except OSError:
            pass
        print(str(error)); return 1


if __name__ == '__main__':
    raise SystemExit(main())
