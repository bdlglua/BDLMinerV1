"""Bounded requests to explicitly configured miners; secrets never enter reports."""
import base64
import json
import math
import os
import re
import ssl
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass


class DeviceError(RuntimeError):
    def __init__(self, message, *, code='device', retryable=False):
        super().__init__(message)
        self.code = code
        self.retryable = retryable


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise DeviceError('Miner redirected the request; check its configured address')


@dataclass(frozen=True)
class Device:
    id: str
    name: str
    url: str
    password_env: str = ''

    def __post_init__(self):
        p = urllib.parse.urlsplit(self.url)
        if (not re.fullmatch(r'[a-zA-Z0-9_-]{1,48}', self.id) or
                p.scheme not in ('http', 'https') or not p.hostname or p.username or
                p.password or p.query or p.fragment or p.path not in ('', '/') or not p.port and p.netloc.endswith(':')):
            raise ValueError('Configure a miner ID and a root HTTP(S) address without credentials')
        if self.password_env and not re.fullmatch(r'[A-Z_][A-Z0-9_]*', self.password_env):
            raise ValueError('Invalid password environment variable name')
        if not self.name or len(self.name) > 80:
            raise ValueError('Miner name must contain 1 to 80 characters')


def load_config(path):
    with open(path, encoding='utf-8') as source:
        config = json.load(source)
    devices = [Device(**d) for d in config.get('miners', [])]
    if not 1 <= len(devices) <= 64 or len({d.id for d in devices}) != len(devices):
        raise ValueError('Configure 1 to 64 miners with unique IDs')
    return config, devices


class Client:
    def __init__(self, device, timeout=8):
        self.device = device
        self.timeout = timeout
        # Preserve environment proxy/CA policy; never forward authentication on redirects.
        self.opener = urllib.request.build_opener(NoRedirect())

    def request(self, path, payload=None, method=None, raw=False, content_type='application/json'):
        if not path.startswith('/') or path.startswith('//'):
            raise ValueError('Device paths must be relative to the configured miner')
        headers = {'Accept': 'application/json', 'User-Agent': 'BDLMiner-host/2',
                   'Origin': self.device.url.rstrip('/')}
        password = os.environ.get(self.device.password_env, '') if self.device.password_env else ''
        if self.device.password_env and not password:
            raise DeviceError('Configured miner password environment variable is unset')
        if password:
            headers['Authorization'] = 'Basic ' + base64.b64encode(('admin:' + password).encode()).decode()
        body = None if payload is None else (payload if isinstance(payload, bytes) else json.dumps(payload, allow_nan=False).encode())
        if body is not None:
            headers['Content-Type'] = content_type
        req = urllib.request.Request(self.device.url.rstrip('/') + path, body, headers, method=method)
        try:
            with self.opener.open(req, timeout=self.timeout) as response:
                data = response.read(131073)
        except urllib.error.HTTPError as error:
            # Device response text can echo settings/passwords: do not expose it.
            raise DeviceError('Miner returned HTTP ' + str(error.code), code='http_' + str(error.code)) from None
        except urllib.error.URLError as error:
            reason = error.reason
            if isinstance(reason, ssl.SSLError):
                raise DeviceError('Miner TLS connection failed', code='tls') from None
            timeout = isinstance(reason, TimeoutError)
            raise DeviceError('Miner request timed out' if timeout else 'Miner connection failed',
                              code='timeout' if timeout else 'connection', retryable=True) from None
        except TimeoutError:
            raise DeviceError('Miner request timed out', code='timeout', retryable=True) from None
        except ssl.SSLError:
            raise DeviceError('Miner TLS connection failed', code='tls') from None
        except OSError:
            raise DeviceError('Miner connection failed', code='connection', retryable=True) from None
        if len(data) > 131072:
            raise DeviceError('Miner response exceeds 128 KiB')
        if raw:
            return data.decode('utf-8', errors='replace')
        try:
            return json.loads(data)
        except (ValueError, UnicodeError):
            raise DeviceError('Miner returned invalid JSON') from None

    def stats(self):
        return sanitize(self.request('/api/stats?history=0'))

    def settings(self):
        data = self.request('/api/settings')
        if not isinstance(data, dict) or not isinstance(data.get('values'), dict):
            raise DeviceError('Miner returned invalid settings')
        return data

    def patch(self, changes):
        return self.request('/api/settings', changes, method='POST', raw=True)

    def diagnostic(self):
        data = self.request('/api/pool/diagnostics')
        if not isinstance(data, dict) or not isinstance(data.get('code'), str):
            raise DeviceError('Pool diagnostics require BDLMiner V2')
        return {k: data[k] for k in DIAGNOSTIC if k in data}


def finite(value, minimum=0):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) and value >= minimum


DIAGNOSTIC = ('code', 'compatible', 'connected', 'work_available', 'protocol', 'required_rolling_bits',
              'version_rolling_bits', 'difficulty', 'expected_share_seconds', 'share_based_hashrate',
              'accepted_samples', 'estimate_window_s', 'checked_ms')


def sanitize(data):
    if not isinstance(data, dict) or not isinstance(data.get('firmware'), str) or not data.get('board'):
        raise DeviceError('Not a BDLMiner statistics response')
    for key in ('uptime_s', 'total_hashes', 'hashrate'):
        if not finite(data.get(key)):
            raise DeviceError('Miner returned invalid counters')
    allowed = ('api_version', 'board', 'firmware', 'firmware_env', 'boot_id', 'uptime_s', 'total_hashes', 'hashrate',
               'hashrate_live', 'temp_c', 'temperature_source', 'max_temp_c', 'power_percent', 'power_limit_percent',
               'limitation_reason', 'heap', 'heap_max_block', 'firmware_trial')
    result = {k: data[k] for k in allowed if k in data}
    fields = {
        'pool': ('connected', 'protocol', 'difficulty', 'host', 'port', 'pending_shares', 'connected_percent'),
        'shares': ('accepted', 'rejected', 'work'),
        'asic': ('model', 'state', 'running', 'frequency_mhz', 'fan_rpm', 'fan_percent', 'fan_auto',
                 'fan_override', 'input_v', 'core_v', 'current_a', 'regulator_temp_c', 'regulator_status',
                 'profile_mhz', 'stock_fallback', 'hardware_error_percent'),
        'github_update': ('checked', 'available', 'installing', 'version', 'channel'),
        'pool_view': ('hashrate', 'updated_s', 'supported'),
        'pool_diagnostics': DIAGNOSTIC,
    }
    for section, keys in fields.items():
        obj = data.get(section)
        if isinstance(obj, dict):
            result[section] = {k: obj[k] for k in keys if k in obj}
    reasons = data.get('shares', {}).get('rejected_reasons', {})
    if isinstance(reasons, dict):
        result.setdefault('shares', {})['rejected_reasons'] = {
            k: reasons[k] for k in ('stale', 'duplicate', 'low_difficulty', 'authorization', 'invalid', 'unknown')
            if finite(reasons.get(k))}
    # Reject non-finite values in stored/API JSON instead of emitting invalid JS numbers.
    def clean(value):
        if isinstance(value, float) and not math.isfinite(value):
            return None
        if isinstance(value, dict):
            return {k: clean(v) for k, v in value.items()}
        return value
    return clean(result)
