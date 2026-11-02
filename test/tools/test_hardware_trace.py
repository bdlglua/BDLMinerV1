"""HTTP phase tracing uses fixtures; these are not physical miner results."""
import copy
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
import urllib.request
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from bdlhost.client import Device, DeviceError
import hardware_suite
import trace_hardware
from test_v2_host import Clock, reading


class Response:
    def __init__(self, clock, body=b'ok', delay=.03, error=None, close_error=None):
        self.clock, self.body, self.delay, self.error, self.close_error = clock, body, delay, error, close_error
        self.closed = False
    def __enter__(self): return self
    def read(self, limit):
        self.clock.wait(self.delay)
        if self.error: raise self.error
        return self.body[:limit]
    def __exit__(self, *args):
        self.closed = True
        if self.close_error: raise self.close_error


class Opener:
    def __init__(self, clock, replies):
        self.clock, self.replies, self.seen = clock, iter(replies), []
    def open(self, request, **kwargs):
        self.seen.append((request.get_method(), request.full_url, kwargs))
        self.clock.wait(.02)
        reply = next(self.replies)
        if isinstance(reply, BaseException): raise reply
        return reply


class HardwareTraceTests(unittest.TestCase):
    def client(self, clock, replies):
        timing = trace_hardware.Timings(clock.now)
        client = trace_hardware.TracingClient(Device('gamma', 'Gamma', 'http://miner'), timing)
        opener = Opener(clock, replies)
        client.opener = trace_hardware.TimedOpener(opener, timing)
        return client, timing, opener

    def test_normal_stats_add_no_other_requests_and_keep_existing_timeout(self):
        clock = Clock()
        client, timing, opener = self.client(clock, [Response(clock, json.dumps(reading()).encode())])
        sample = client.stats()
        self.assertEqual(len(opener.seen), 1)
        self.assertEqual(opener.seen[0], ('GET', 'http://miner/api/stats?history=0', {'timeout': 8}))
        self.assertEqual(client.context, [])
        self.assertEqual(timing.summary()['stats_requests'], 1)
        self.assertAlmostEqual(timing.summary()['stats_max_ms'], 50)
        self.assertEqual(timing.summary()['slow_or_failed_requests'], [])
        self.assertNotIn('HIDDEN', json.dumps(sample))

    def test_header_and_body_timeouts_are_distinguished_without_exception_contents(self):
        for phase in ('open_headers', 'read_body'):
            clock = Clock()
            reply = TimeoutError('HIDDEN header credential') if phase == 'open_headers' else Response(
                clock, delay=8, error=TimeoutError('HIDDEN body data'))
            client, timing, _ = self.client(clock, [reply])
            with self.assertRaises(DeviceError): client.stats()
            event = timing.summary()['slow_or_failed_requests'][0]
            self.assertEqual(event['phase'], phase)
            self.assertEqual(event['error_type'], 'TimeoutError')
            self.assertEqual(event['bytes_returned'], 0)
            self.assertEqual(timing.summary()['request_exceptions'], 1)
            self.assertNotIn('HIDDEN', json.dumps(timing.summary()))

    def test_response_close_error_is_recorded_and_connection_released(self):
        clock = Clock()
        reply = Response(clock, json.dumps(reading()).encode(), close_error=OSError('HIDDEN'))
        client, timing, _ = self.client(clock, [reply])
        with self.assertRaises(DeviceError): client.stats()
        self.assertTrue(reply.closed)
        self.assertEqual(timing.summary()['slow_or_failed_requests'][0]['phase'], 'close')
        self.assertNotIn('HIDDEN', json.dumps(timing.summary()))

    def test_recovery_captures_only_safe_diagnostics_and_stall_numbers(self):
        clock = Clock()
        diagnostic = {'uptime_s': 500, 'ssid': 'HIDDEN', 'tls': {'handshakes': 3, 'last_ms': 800, 'password': 'HIDDEN'},
                      'engines': [{'index': 0, 'name': 'HIDDEN', 'waiting_ms': 10, 'wallet': 'HIDDEN'}]}
        log = b'00:08:00 [cfg] wallet HIDDEN\n00:08:01 [loop] stalled 9000 ms\n00:08:02 [wifi] HIDDEN\n'
        client, timing, opener = self.client(clock, [TimeoutError('HIDDEN'),
            Response(clock, json.dumps(reading()).encode()), Response(clock, json.dumps(diagnostic).encode()), Response(clock, log)])
        with self.assertRaises(DeviceError): client.stats()
        sample = client.stats()
        self.assertEqual(len(opener.seen), 4)
        self.assertTrue(all(row[0] == 'GET' for row in opener.seen))
        self.assertEqual(client.context[0]['main_loop_stalls'], [{'device_uptime_hms': '00:08:01', 'stalled_ms': 9000}])
        self.assertEqual(client.context[0]['diagnostics']['tls'], {'handshakes': 3, 'last_ms': 800})
        self.assertNotIn('HIDDEN', json.dumps({'context': client.context, 'timing': timing.summary(), 'sample': sample}))

    def test_failed_metadata_is_bounded_and_does_not_store_request_credentials(self):
        clock = Clock(); timing = trace_hardware.Timings(clock.now)
        opener = trace_hardware.TimedOpener(Opener(clock, [TimeoutError('HIDDEN')] * 40), timing)
        req = urllib.request.Request('http://miner/api/stats?token=HIDDEN', headers={'Authorization': 'Basic HIDDEN'})
        for _ in range(40):
            with self.assertRaises(TimeoutError): opener.open(req, timeout=8)
        summary = timing.summary()
        self.assertEqual(summary['request_exceptions'], 40)
        self.assertEqual(len(summary['slow_or_failed_requests']), 32)
        self.assertEqual(summary['events_omitted'], 8)
        self.assertNotIn('HIDDEN', json.dumps(summary))

    def test_original_review_criteria_survive_timing_and_recovered_context_probes(self):
        clock = Clock(); timing = trace_hardware.Timings(clock.now)
        client = trace_hardware.TracingClient(Device('gamma', 'Gamma', 'http://miner'), timing)
        class DynamicOpener:
            stats_calls = 0
            def open(self, request, **kwargs):
                if '/api/stats' in request.full_url:
                    self.stats_calls += 1
                    if self.stats_calls == 2: raise TimeoutError('transport')
                    sample = copy.deepcopy(reading())
                    sample['uptime_s'] += clock.value
                    sample['total_hashes'] += clock.value * 1e12
                    return Response(clock, json.dumps(sample).encode(), delay=0)
                return Response(clock, b'{}' if '/diagnostics' in request.full_url else b'', delay=0)
        client.opener = trace_hardware.TimedOpener(DynamicOpener(), timing)
        with patch.object(hardware_suite.time, 'monotonic', clock.now), patch.object(hardware_suite.time, 'sleep', clock.wait):
            report = hardware_suite.measure_counters(client, 60)
        self.assertEqual(report['status'], 'review')
        self.assertEqual(report['read_error_count'], 1)
        self.assertEqual(report['read_recoveries'], 1)
        self.assertEqual(len(client.context), 1)
        self.assertAlmostEqual(report['hashrate_hs'], 1e12)

    def test_interrupt_replaces_an_old_success_report(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / 'trace.json'; out.write_text('{"status":"passed"}')
            argv = ['trace_hardware.py', '--config', 'unused', '--miner', 'gamma', '--output', str(out)]
            with patch('sys.argv', argv), patch.object(trace_hardware, 'load_config',
                    return_value=({}, [Device('gamma', 'Gamma', 'http://miner')])), \
                 patch.object(trace_hardware, 'measure_counters', side_effect=KeyboardInterrupt), \
                 patch('sys.stdout', new=io.StringIO()):
                self.assertEqual(trace_hardware.main(), 1)
            self.assertEqual(json.loads(out.read_text())['status'], 'failed')


if __name__ == '__main__': unittest.main()
