"""Loopback-only simulated miners for browser regression checks; not hardware data."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import sys
import tempfile
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from test_v2_host import reading
from bdlhost.client import Device
from bdlhost.hub import Fleet, handler

power = 100


class Miner(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith('/api/stats'):
            data = reading()
            data['power_limit_percent'] = power
            data['power_percent'] = power // 2
            data['pool']['protocol'] = 'Stratum V2 (Noise)'
            data['github_update'] = {'available': False, 'channel': 'beta'}
        elif self.path == '/api/pool/diagnostics':
            data = {'code': 'ready', 'compatible': True, 'protocol': 'Stratum V2 (Noise)',
                    'accepted_samples': 10, 'share_based_hashrate': 1.1e12, 'expected_share_seconds': 43}
        else:
            data = {'values': {'power': power}}
        body = json.dumps(data).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        global power
        data = json.loads(self.rfile.read(int(self.headers.get('Content-Length', 0))))
        power = data.get('power', power)
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b'{"restart":false,"changed":1}')

    def log_message(self, *_): pass


with tempfile.TemporaryDirectory() as tmp:
    miner = ThreadingHTTPServer(('127.0.0.1', 0), Miner)
    threading.Thread(target=miner.serve_forever, daemon=True).start()
    fleet = Fleet([Device('gamma', 'Gamma 601 · Fixture', f'http://127.0.0.1:{miner.server_port}'),
                   Device('m5', 'M5StickC Plus2 · Fixture', 'http://127.0.0.1:9')], Path(tmp) / 'data.sqlite')
    hosts = set()
    hub = ThreadingHTTPServer(('127.0.0.1', 0), handler(fleet, 'fixture-token', hosts))
    hosts.add(f'127.0.0.1:{hub.server_port}')
    fleet.start()
    fleet.poll_one('gamma')
    with fleet.lock:
        for age in range(3600, 0, -60):
            fleet.db.execute('INSERT INTO readings VALUES (?,?,?,?,?)',
                             ('gamma', time.time() - age, 'fixture', 1e12 + age * 1e7, 53))
        fleet.db.commit()
    print(f'http://127.0.0.1:{hub.server_port}', flush=True)
    try:
        hub.serve_forever()
    finally:
        fleet.close()
        hub.server_close()
        miner.server_close()
