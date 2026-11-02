#!/usr/bin/env python3
"""Observe a manual network interruption; never change router/miner settings."""
import argparse
from datetime import datetime, timezone
from pathlib import Path
import time

from bdlhost.client import Client, DeviceError, load_config
from check_hardware import write_report
from hardware_suite import counter_measure, health_issues


def observe(client, kind='wifi', timeout=180, confirm=None, clock=time.monotonic,
            wait=time.sleep, notify=print):
    report = {'kind': 'manual-network-recovery', 'tool_version': 1,
              'recorded_utc': datetime.now(timezone.utc).isoformat(),
              'simulated': False, 'read_only': True, 'outage_kind': kind,
              'status': 'failed', 'observed_outage': False, 'restored': False,
              'operator_confirmed': False, 'samples': 0, 'first': None, 'last': None}
    started = clock()
    armed = False
    previous = None

    def reading():
        nonlocal previous
        sample = client.stats()
        report['last'] = sample
        report['samples'] += 1
        first = report['first']
        if first:
            for key in ('board', 'firmware_env', 'firmware', 'boot_id'):
                if sample.get(key) != first.get(key):
                    raise ValueError('Device identity, firmware or boot changed during the network test')
        if sample.get('firmware_trial') is not False:
            raise ValueError('Firmware must stay confirmed during the network test')
        if previous and (sample['uptime_s'] < previous['uptime_s'] or
                         sample['total_hashes'] < previous['total_hashes']):
            raise ValueError('Miner counters reset during the network test')
        issues = health_issues(sample)
        if issues:
            report['health_issues'] = issues
            raise ValueError('Hardware health check failed')
        previous = sample
        return sample

    def mining(sample):
        return (sample.get('pool', {}).get('connected') is True and
                sample['hashrate'] > 0 and sample.get('power_percent', 0) > 0)

    def unavailable(error, stage):
        # HTTP/auth/TLS/malformed replies are not evidence of a network cut.
        if not error.retryable or error.code not in ('timeout', 'connection'):
            raise error
        report['read_error_count'] = report.get('read_error_count', 0) + 1
        report['health_during_transport_gaps'] = 'unobserved'
        events = report.setdefault('read_errors', [])
        if len(events) < 32:
            events.append({'elapsed_s': round(clock() - started, 3),
                           'stage': stage, 'code': error.code})
        else:
            report['read_error_events_omitted'] = report.get('read_error_events_omitted', 0) + 1

    try:
        if kind not in ('wifi', 'pool') or not 60 <= timeout <= 900:
            raise ValueError('Use wifi/pool and a timeout between 60 and 900 seconds')
        first = reading()
        report['first'] = first
        if not first.get('boot_id') or first.get('firmware_trial') is not False or not mining(first):
            raise ValueError('Start with confirmed firmware, a boot ID and healthy active mining')
        armed = True
        notify('Observer armed. Interrupt only the selected miner\'s ' + kind +
               ' connection now. Keep this computer connected; do not power off the miner.')
        deadline = clock() + timeout
        consecutive_errors = 0
        while clock() < deadline:
            wait(2)
            try:
                sample = reading()
                consecutive_errors = 0
                if kind == 'pool' and sample.get('pool', {}).get('connected') is False:
                    report['observed_outage'] = True
                    report['outage_evidence'] = 'reachable API reported pool disconnected'
                    break
            except DeviceError as error:
                unavailable(error, 'outage')
                consecutive_errors += 1
                if kind == 'wifi' and consecutive_errors >= 2:
                    report['observed_outage'] = True
                    report['outage_evidence'] = 'two consecutive HTTP transport failures; operator attribution required'
                    break
        if not report['observed_outage']:
            raise ValueError('No matching outage observed before timeout')
        notify('Outage observed. Restore the connection now; waiting for mining to resume on the same boot.')
        deadline = clock() + timeout
        resumed = None
        while clock() < deadline:
            wait(2)
            try:
                sample = reading()
            except DeviceError as error:
                unavailable(error, 'recovery')
                resumed = None
                continue
            if not mining(sample):
                resumed = None
                continue
            if resumed is None:
                resumed = sample
            if sample['uptime_s'] - resumed['uptime_s'] >= 10:
                measured = counter_measure(resumed, sample)
                if measured['hashrate_hs'] > 0:
                    report.update(restored=True, recovery_window=measured, first_resumed=resumed)
                    # An HTTP gap alone cannot establish that Wi-Fi was cut.
                    report['operator_confirmed'] = confirm() is True if confirm else False
                    report['status'] = 'passed' if report['operator_confirmed'] else 'review'
                    if not report['operator_confirmed']:
                        report['review_reason'] = 'Operator did not confirm the deliberate interruption/restoration'
                    return report
        raise ValueError('Healthy mining did not resume before timeout')
    except (Exception, KeyboardInterrupt) as error:
        report['error'] = str(error) if isinstance(error, (DeviceError, ValueError)) else type(error).__name__
        return report
    finally:
        report['elapsed_s'] = round(clock() - started, 3)
        if armed and not report['restored']:
            notify('Ensure the connection is restored now. This read-only observer cannot restore router settings.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--miner', required=True)
    parser.add_argument('--kind', choices=('wifi', 'pool'), default='wifi')
    parser.add_argument('--timeout', type=int, default=180, help='Seconds allowed for each outage/recovery phase')
    parser.add_argument('--output', type=Path, default=Path('network-recovery.json'))
    args = parser.parse_args()
    if not 60 <= args.timeout <= 900:
        parser.error('Use a timeout between 60 and 900 seconds')
    _, devices = load_config(args.config)
    device = next((d for d in devices if d.id == args.miner), None)
    if device is None:
        parser.error('Unknown configured miner')

    def confirm():
        return input('Did you interrupt/restore this miner\'s connection while this computer stayed connected (yes/no)? ').strip().lower() in ('yes', 'y', 'oui', 'o')

    report = observe(Client(device), args.kind, args.timeout, confirm=confirm)
    report['miner_id'] = device.id
    write_report(args.output, report)
    print('Network recovery: ' + report['status'] + '\nReport: ' + str(args.output))
    return {'passed': 0, 'failed': 1, 'review': 2}[report['status']]


if __name__ == '__main__':
    raise SystemExit(main())
