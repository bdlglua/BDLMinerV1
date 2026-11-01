"""Supervised Gamma frequency trials. No voltage changes; restore on every exit."""
import threading
import time
from .client import DeviceError, finite


class CalibrationError(RuntimeError):
    pass


def restore_settings(client, baseline, clock=time.monotonic, wait=time.sleep):
    if (set(baseline) != {'asicmhz', 'fanauto', 'fanpct'} or
            baseline['asicmhz'] not in range(525, 801, 25) or type(baseline['fanauto']) is not bool or
            type(baseline['fanpct']) is not int or not 30 <= baseline['fanpct'] <= 100):
        raise CalibrationError('Invalid Gamma restoration checkpoint')
    client.patch(baseline)
    deadline = clock() + 120
    while clock() < deadline:
        try:
            values = client.settings()['values']
            sample = client.stats()
            if (all(values.get(k) == v for k, v in baseline.items()) and
                    sample.get('asic', {}).get('running') and
                    sample['asic'].get('frequency_mhz') == baseline['asicmhz']):
                return
        except DeviceError:
            pass
        wait(2)
    raise CalibrationError('Original Gamma settings were not verified within 120 seconds')


class Calibrator:
    def __init__(self, client, frequencies=(525, 550, 575, 600), warmup=60, seconds=180,
                 interval=2, cancel=None, progress=None, clock=time.monotonic, wait=None, checkpoint=None):
        self.client = client
        self.frequencies = tuple(frequencies)
        if (not self.frequencies or len(self.frequencies) > 12 or len(set(self.frequencies)) != len(self.frequencies) or
                any(type(n) is not int or n not in range(525, 801, 25) for n in self.frequencies)):
            raise ValueError('Use 525 to 800 MHz in 25 MHz steps (at most 12 trials)')
        if warmup < 60 or seconds < 120 or not 2 <= interval <= 10:
            raise ValueError('Use warmup >= 60 s, measurement >= 120 s and interval 2 to 10 s')
        self.warmup, self.seconds, self.interval = warmup, seconds, interval
        self.cancel = cancel or threading.Event()
        self.progress = progress or (lambda data: None)
        self.checkpoint = checkpoint or (lambda data: None)
        self.clock = clock
        self.wait = wait or self.cancel.wait
        self.limit = 63

    def check(self, sample, require_ready=True):
        if self.cancel.is_set():
            raise CalibrationError('Calibration cancelled')
        a = sample.get('asic', {})
        if sample.get('firmware_env') != 'bitaxe-gamma-601' or a.get('model') != 'BM1370':
            raise CalibrationError('Calibration supports the Gamma 601 BM1370 only')
        if sample.get('firmware_trial'):
            raise CalibrationError('Confirm the firmware trial before calibration')
        if sample.get('github_update', {}).get('installing'):
            raise CalibrationError('Wait for the firmware update before calibration')
        if require_ready and (a.get('running') is not True or sample.get('pool', {}).get('connected') is not True):
            raise CalibrationError('ASIC or pool stopped during calibration')
        for key, low, high in (('input_v', 4.6, 5.5), ('core_v', 1.0, 1.25),
                                ('regulator_temp_c', 0, 85), ('current_a', 0, 25)):
            value = a.get(key)
            if not finite(value, low) or value >= high:
                raise CalibrationError('Missing or unsafe Gamma telemetry: ' + key)
        if not finite(sample.get('temp_c')) or sample['temp_c'] >= self.limit:
            raise CalibrationError('ASIC temperature reached the calibration limit or is unavailable')
        if not finite(a.get('fan_rpm'), 500) or a.get('regulator_status') != 0:
            raise CalibrationError('Fan or regulator health check failed')
        error = a.get('hardware_error_percent')
        if not finite(error) or error > 0.5:
            raise CalibrationError('Hardware errors exceed 0.5% or are unavailable')
        if sample.get('power_percent') != 100 or sample.get('limitation_reason') != 'full' or a.get('stock_fallback'):
            raise CalibrationError('Full power without throttling/fallback is required')
        return sample

    def ready(self, frequency):
        deadline = self.clock() + 120
        while self.clock() < deadline:
            if self.cancel.is_set():
                raise CalibrationError('Calibration cancelled')
            try:
                sample = self.client.stats()
            except DeviceError:
                self.wait(self.interval)
                continue
            a = sample.get('asic', {})
            if a.get('running') and a.get('frequency_mhz') == frequency:
                return self.check(sample)
            # Starting a stopped ASIC is handled by its firmware interlocks. Stop
            # waiting immediately on a reported thermal/regulator fault.
            if finite(sample.get('temp_c')) and sample['temp_c'] >= self.limit:
                raise CalibrationError('ASIC temperature reached the calibration limit')
            if a.get('running'):
                self.check(sample)
            self.wait(self.interval)
        raise CalibrationError('Gamma did not resume at the requested frequency within 120 seconds')

    def observe(self, duration, baseline):
        first = baseline
        previous = baseline
        deadline = self.clock() + duration
        maximum = baseline['temp_c']
        while self.clock() < deadline:
            self.wait(min(self.interval, max(0, deadline - self.clock())))
            sample = self.check(self.client.stats())
            if (sample.get('boot_id') != first.get('boot_id') or sample['uptime_s'] < previous['uptime_s'] or
                    sample['total_hashes'] < previous['total_hashes'] or
                    sample['asic'].get('frequency_mhz') != first['asic'].get('frequency_mhz')):
                raise CalibrationError('Miner rebooted, reset counters or changed frequency during a trial')
            maximum = max(maximum, sample['temp_c'])
            previous = sample
        return previous, maximum

    def run(self):
        report = {'status': 'running', 'trials': [], 'restored': False, 'recommended_mhz': None,
                  'rate_source': 'delta total_hashes / delta device uptime_s',
                  'note': 'Core current is not input current. No voltage setting is changed.'}
        baseline = None
        try:
            sample = self.client.stats()
            values = self.client.settings()['values']
            maximum = values.get('maxtemp', 65)
            self.limit = min(63, maximum - 2 if finite(maximum, 5) else 63)
            self.check(sample)
            if values.get('asicmhz') not in range(525, 801, 25):
                raise CalibrationError('Original Gamma frequency is not restorable')
            baseline = {key: values[key] for key in ('asicmhz', 'fanauto', 'fanpct')}
            self.checkpoint(baseline)
            for mhz in self.frequencies:
                if self.cancel.is_set():
                    raise CalibrationError('Calibration cancelled')
                self.progress({'stage': 'starting', 'frequency_mhz': mhz})
                self.client.patch({'asicmhz': mhz, 'fanauto': True})
                sample = self.ready(mhz)
                self.progress({'stage': 'warmup', 'frequency_mhz': mhz})
                first, warm_max = self.observe(self.warmup, sample)
                self.progress({'stage': 'measuring', 'frequency_mhz': mhz})
                last, measured_max = self.observe(self.seconds, first)
                dt = last['uptime_s'] - first['uptime_s']
                if dt <= 0 or not first.get('boot_id'):
                    raise CalibrationError('Insufficient device counter evidence')
                rate = (last['total_hashes'] - first['total_hashes']) / dt
                if not finite(rate, 1):
                    raise CalibrationError('No measured work during frequency trial')
                trial = {'frequency_mhz': mhz, 'hashrate_hs': rate, 'device_seconds': dt,
                         'max_temperature_c': max(warm_max, measured_max),
                         'hardware_error_percent': last['asic']['hardware_error_percent'],
                         'boot_id': first['boot_id'], 'first_uptime_s': first['uptime_s'],
                         'first_total_hashes': first['total_hashes'], 'last_total_hashes': last['total_hashes']}
                report['trials'].append(trial)
                self.progress({'stage': 'trial_complete', **trial})
            best = max(t['hashrate_hs'] for t in report['trials'])
            report['recommended_mhz'] = min(t['frequency_mhz'] for t in report['trials'] if t['hashrate_hs'] >= best * .97)
            report['status'] = 'completed'
        except Exception as error:
            report['status'] = 'cancelled' if self.cancel.is_set() else 'failed'
            report['error'] = str(error) if isinstance(error, (CalibrationError, DeviceError)) else type(error).__name__
        finally:
            if baseline is not None:
                self.progress({'stage': 'restoring'})
                try:
                    # Cancellation never cancels restoration. Verify saved settings
                    # and an actual ready ASIC after the possible restore reboot.
                    restore_settings(self.client, baseline, self.clock,
                                     time.sleep if self.clock is time.monotonic else self.wait)
                    self.checkpoint(None)
                    report['restored'] = True
                except Exception as error:
                    report['status'] = 'failed'
                    report['restore_error'] = str(error) if isinstance(error, (CalibrationError, DeviceError)) else type(error).__name__
        self.progress({'stage': 'finished', 'report': report})
        return report
