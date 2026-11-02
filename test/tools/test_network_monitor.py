"""Manual-fault observer regressions with controlled clocks, not physical tests."""
import copy
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from bdlhost.client import Device, DeviceError, sanitize
import monitor_network
from test_v2_host import Clock, reading


class Miner:
    def __init__(self, clock, changes=None):
        self.clock = clock
        self.changes = changes or {}
        self.calls = 0

    def stats(self):
        change = self.changes.get(self.calls, {})
        self.calls += 1
        if isinstance(change, BaseException):
            raise change
        sample = reading()
        sample['uptime_s'] += self.clock.value
        sample['total_hashes'] += self.clock.value * 1e12
        sample.update(copy.deepcopy(change))
        return sanitize(sample)


class NetworkMonitorTests(unittest.TestCase):
    def run_observer(self, changes=None, kind='wifi', confirmed=True, client=None):
        clock = Clock()
        miner = client or Miner(clock, changes)
        messages = []
        result = monitor_network.observe(miner, kind, timeout=60, confirm=lambda: confirmed,
                                         clock=clock.now, wait=clock.wait, notify=messages.append)
        self.assertNotIn('HIDDEN', json.dumps(result))
        return result, messages

    @staticmethod
    def outage():
        return {1: DeviceError('timeout', code='timeout', retryable=True),
                2: DeviceError('connection', code='connection', retryable=True)}

    def test_deliberate_cut_requires_transport_loss_same_boot_and_actual_counter_progress(self):
        result, messages = self.run_observer(self.outage())
        self.assertEqual(result['status'], 'passed')
        self.assertTrue(result['read_only'])
        self.assertTrue(result['observed_outage'] and result['operator_confirmed'] and result['restored'])
        self.assertEqual(result['read_error_count'], 2)
        self.assertEqual(result['first']['boot_id'], result['last']['boot_id'])
        self.assertEqual(result['recovery_window'], {'device_seconds': 10, 'hashrate_hs': 1e12})
        self.assertTrue(any('Restore the connection' in text for text in messages))

    def test_http_gap_without_operator_attribution_is_review(self):
        result, _ = self.run_observer(self.outage(), confirmed=False)
        self.assertEqual(result['status'], 'review')
        self.assertTrue(result['restored'])
        self.assertFalse(result['operator_confirmed'])

    def test_pool_test_needs_reachable_api_reporting_disconnection(self):
        result, _ = self.run_observer({1: {'pool': {'connected': False}}}, kind='pool')
        self.assertEqual(result['status'], 'passed')
        result, _ = self.run_observer(self.outage(), kind='pool')
        self.assertEqual(result['status'], 'failed')
        self.assertFalse(result['observed_outage'])

    def test_no_cut_or_one_transient_does_not_pass(self):
        for changes in ({}, {1: DeviceError('timeout', code='timeout', retryable=True)}):
            result, messages = self.run_observer(changes)
            self.assertEqual(result['status'], 'failed')
            self.assertFalse(result['observed_outage'])
            self.assertIn('Ensure the connection is restored', messages[-1])

    def test_auth_tls_and_malformed_data_are_not_network_outages(self):
        for code in ('http_401', 'http_503', 'tls', 'device'):
            result, _ = self.run_observer({1: DeviceError('unusable reply', code=code)})
            self.assertEqual(result['status'], 'failed')
            self.assertFalse(result['observed_outage'])

    def test_reboot_firmware_identity_health_or_counter_reset_cannot_pass(self):
        for change in ({'boot_id': 'reboot'}, {'firmware': 'different'}, {'firmware_env': 'other'},
                       {'firmware_trial': True}, {'total_hashes': 1}, {'uptime_s': 1}, {'temp_c': 75}):
            changes = self.outage()
            changes[3] = change
            result, _ = self.run_observer(changes)
            self.assertEqual(result['status'], 'failed', change)
            self.assertFalse(result['restored'])

    def test_no_cut_is_requested_during_trial_or_without_active_mining(self):
        for change in ({'firmware_trial': True}, {'firmware_trial': None}, {'boot_id': ''},
                       {'power_percent': 0}, {'hashrate': 0}, {'pool': {'connected': False}}):
            result, messages = self.run_observer({0: change})
            self.assertEqual(result['status'], 'failed')
            self.assertEqual(messages, [])

    def test_no_restoration_frozen_counters_and_interruption_cannot_pass(self):
        for failure in ('unreachable', 'frozen', 'interrupt'):
            clock = Clock()
            class BrokenMiner(Miner):
                def stats(self):
                    if self.calls >= 3 and failure == 'unreachable':
                        raise DeviceError('timeout', code='timeout', retryable=True)
                    if self.calls == 3 and failure == 'interrupt':
                        raise KeyboardInterrupt()
                    sample = super().stats()
                    if failure == 'frozen': sample['total_hashes'] = 5e14
                    return sample
            miner = BrokenMiner(clock, self.outage())
            messages = []
            result = monitor_network.observe(miner, timeout=60, confirm=lambda: True,
                                             clock=clock.now, wait=clock.wait, notify=messages.append)
            self.assertEqual(result['status'], 'failed', failure)
            self.assertFalse(result['restored'])
            self.assertIn('Ensure the connection is restored', messages[-1])

    def test_cli_review_replaces_previous_pass_with_exit_two(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'report.json'
            path.write_text('{"status":"passed"}')
            argv = ['monitor_network.py', '--config', 'unused.json', '--miner', 'm5', '--output', str(path)]
            with patch('sys.argv', argv), patch.object(monitor_network, 'load_config',
                    return_value=({}, [Device('m5', 'M5', 'http://miner')])), \
                 patch.object(monitor_network, 'observe', return_value={'status': 'review'}), \
                 patch('sys.stdout', new=io.StringIO()):
                self.assertEqual(monitor_network.main(), 2)
            self.assertEqual(json.loads(path.read_text())['status'], 'review')


if __name__ == '__main__':
    unittest.main()
