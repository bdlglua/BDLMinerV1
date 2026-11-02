#!/usr/bin/env python3
"""Read-only counter/health bench with bounded, credential-free HTTP timings.

Normal sampling is identical to hardware_suite. After a recovered transport
error only, collect safe diagnostic numbers and main-loop stall durations.
The request timeout, retry rules and pass/review/fail criteria are unchanged.
"""
import argparse
from datetime import datetime, timezone
from pathlib import Path
import re
import time
import urllib.parse

from bdlhost.client import Client, DeviceError, finite, load_config
from check_hardware import write_report
from hardware_suite import measure_counters


class Timings:
    def __init__(self, clock=time.monotonic):
        self.clock = clock
        self.started = clock()
        self.count = 0
        self.stats_count = 0
        self.stats_total_ms = 0
        self.stats_max_ms = 0
        self.error_count = 0
        self.events = []
        self.omitted = 0

    def record(self, operation, start, headers, body_start, size, phase, error=None):
        end = self.clock()
        duration = max(0, end - start) * 1000
        self.count += 1
        if operation == 'stats':
            self.stats_count += 1
            self.stats_total_ms += duration
            self.stats_max_ms = max(self.stats_max_ms, duration)
        self.error_count += error is not None
        if error is not None or duration >= 1000:
            event = {'elapsed_s': round(end - self.started, 3), 'operation': operation,
                     'phase': phase, 'total_ms': round(duration, 3), 'bytes_returned': size}
            if headers is not None:
                event['connect_headers_ms'] = round(max(0, headers - start) * 1000, 3)
            if body_start is not None:
                event['body_ms'] = round(max(0, end - body_start) * 1000, 3)
            if error is not None:
                event['error_type'] = type(error).__name__  # never exception text/URLs/headers
            if len(self.events) < 32:
                self.events.append(event)
            else:
                self.omitted += 1

    def summary(self):
        return {'requests': self.count, 'stats_requests': self.stats_count,
                'request_exceptions': self.error_count,
                'stats_mean_ms': round(self.stats_total_ms / self.stats_count, 3) if self.stats_count else None,
                'stats_max_ms': round(self.stats_max_ms, 3),
                'slow_or_failed_requests': self.events, 'events_omitted': self.omitted,
                'phases': 'open_headers includes proxy/DNS/TCP/response headers; read_body includes body receipt'}


class TimedResponse:
    def __init__(self, response, timings, operation, start):
        self.response, self.timings, self.operation, self.start = response, timings, operation, start
        self.headers = timings.clock()
        self.body_start = None
        self.size = 0
        self.phase = 'open_headers'

    def __enter__(self):
        try:
            self.response.__enter__()
        except BaseException as error:
            self.timings.record(self.operation, self.start, self.headers, None, 0, 'open_headers', error)
            raise
        return self

    def read(self, size):
        self.phase = 'read_body'
        self.body_start = self.timings.clock()
        data = self.response.read(size)
        self.size += len(data)
        self.phase = 'complete'
        return data

    def __exit__(self, exc_type, error, traceback):
        try:
            result = self.response.__exit__(exc_type, error, traceback)
        except BaseException as close_error:
            self.timings.record(self.operation, self.start, self.headers, self.body_start,
                                self.size, 'close', close_error)
            raise
        else:
            self.timings.record(self.operation, self.start, self.headers, self.body_start,
                                self.size, self.phase, error)
            return result


class TimedOpener:
    def __init__(self, opener, timings):
        self.opener, self.timings = opener, timings

    def open(self, request, **kwargs):
        path = urllib.parse.urlsplit(request.full_url).path
        operation = {'/api/stats': 'stats', '/api/diagnostics': 'diagnostics', '/api/log': 'log'}.get(path, 'other')
        start = self.timings.clock()
        try:
            response = self.opener.open(request, **kwargs)
        except BaseException as error:
            self.timings.record(operation, start, None, None, 0, 'open_headers', error)
            raise
        return TimedResponse(response, self.timings, operation, start)


def safe_diagnostics(data):
    if not isinstance(data, dict):
        raise ValueError('Invalid diagnostics')
    result = {key: data[key] for key in ('uptime_s',) if finite(data.get(key))}
    tls = data.get('tls', {})
    if isinstance(tls, dict):
        result['tls'] = {key: tls[key] for key in ('handshakes', 'failures', 'total_ms', 'last_ms') if finite(tls.get(key))}
    engines = data.get('engines', [])
    if isinstance(engines, list):
        result['engines'] = [{key: row[key] for key in ('index', 'hashing_ms', 'rest_ms', 'waiting_ms', 'paused_ms')
                              if finite(row.get(key))} for row in engines[:8] if isinstance(row, dict)]
    return result


class TracingClient(Client):
    def __init__(self, device, timings):
        super().__init__(device)  # retain the standard proxy, CA, redirect, auth and eight-second timeout rules
        self.timings = timings
        self.opener = TimedOpener(self.opener, timings)
        self.need_context = False
        self.context = []
        self.first_stats = self.last_stats = None
        self.stats_samples = 0

    def stats(self):
        try:
            sample = super().stats()
        except DeviceError as error:
            if error.retryable and error.code in ('timeout', 'connection'):
                self.need_context = True
            raise
        self.stats_samples += 1
        if self.first_stats is None:
            self.first_stats = sample
        self.last_stats = sample
        if self.need_context and len(self.context) < 3:
            self.need_context = False
            context = {'after_recovery_elapsed_s': round(self.timings.clock() - self.timings.started, 3),
                       'boot_id': sample.get('boot_id'), 'uptime_s': sample.get('uptime_s')}
            try:
                context['diagnostics'] = safe_diagnostics(self.request('/api/diagnostics'))
            except Exception as error:
                context['diagnostic_error_type'] = type(error).__name__
            try:
                text = self.request('/api/log', raw=True)
                # No raw log text: Wi-Fi names, wallet, addresses and arbitrary messages stay on the device.
                context['main_loop_stalls'] = [{'device_uptime_hms': match[0], 'stalled_ms': int(match[1])}
                    for match in re.findall(r'(?m)^(\d{2}:\d{2}:\d{2}) \[loop\] stalled (\d+) ms\s*$', text)][-16:]
            except Exception as error:
                context['log_error_type'] = type(error).__name__
            self.context.append(context)
        return sample


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--miner', required=True)
    parser.add_argument('--seconds', type=int, default=3600)
    parser.add_argument('--dashboard', choices=('closed', 'open', 'unspecified'), default='unspecified')
    parser.add_argument('--hub', choices=('stopped', 'running', 'unspecified'), default='unspecified')
    parser.add_argument('--output', type=Path, default=Path('hardware-trace.json'))
    args = parser.parse_args()
    if not 60 <= args.seconds <= 86400:
        parser.error('Measure between 60 and 86400 seconds')
    _, devices = load_config(args.config)
    device = next((d for d in devices if d.id == args.miner), None)
    if device is None:
        parser.error('Unknown configured miner')
    timings = Timings()
    client = TracingClient(device, timings)
    print('Read-only timed bench started; default timeouts and review criteria retained.', flush=True)
    try:
        result = measure_counters(client, args.seconds)
    except KeyboardInterrupt:
        result = {'status': 'failed', 'error': 'Interrupted', 'first': client.first_stats,
                  'last': client.last_stats, 'samples': client.stats_samples}
    report = {'recorded_utc': datetime.now(timezone.utc).isoformat(), 'kind': 'traced-hardware-bench',
              'tool_version': 1, 'simulated': False, 'read_only': True, 'miner_id': device.id,
              'conditions_operator_declared': {'dashboard': args.dashboard, 'hub': args.hub},
              'status': result['status'], 'counters_and_health': result,
              'http_timing': timings.summary(), 'after_recovered_errors': client.context}
    write_report(args.output, report)
    print('Result: ' + report['status'] + '\nReport: ' + str(args.output))
    return {'passed': 0, 'failed': 1, 'review': 2}[report['status']]


if __name__ == '__main__':
    raise SystemExit(main())
