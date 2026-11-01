#!/usr/bin/env python3
"""Run supervised frequency trials on a configured Gamma, restoring its settings."""
import argparse
import json
import os
from pathlib import Path
from bdlhost.client import Client, load_config
from bdlhost.calibration import Calibrator, restore_settings


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config', type=Path, required=True)
    p.add_argument('--miner', required=True)
    p.add_argument('--frequencies', default='525,550,575,600')
    p.add_argument('--warmup', type=int, default=60)
    p.add_argument('--seconds', type=int, default=180)
    p.add_argument('--output', type=Path, default=Path('gamma-calibration.json'))
    p.add_argument('--restore', action='store_true', help='Restore the saved checkpoint without running trials')
    args = p.parse_args()
    _, devices = load_config(args.config)
    device = next((d for d in devices if d.id == args.miner), None)
    if device is None:
        p.error('Unknown configured miner')
    recovery = args.output.with_suffix('.recovery.json')
    client = Client(device)
    if recovery.exists():
        saved = json.loads(recovery.read_text())
        if saved['miner_id'] != device.id or saved['url'] != device.url:
            p.error('Checkpoint belongs to a different miner')
        restore_settings(client, saved['settings'])
        recovery.unlink()
        print('Interrupted calibration restored and verified. Run again to begin new trials.')
        return
    if args.restore:
        p.error('No calibration checkpoint to restore')
    def checkpoint(settings):
        if settings is None:
            recovery.unlink(missing_ok=True)
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            pending = recovery.with_suffix('.tmp')
            pending.write_text(json.dumps({'miner_id': device.id, 'url': device.url, 'settings': settings}))
            os.replace(pending, recovery)
    import threading
    import signal
    cancel = threading.Event()
    signal.signal(signal.SIGINT, lambda *_: cancel.set())
    signal.signal(signal.SIGTERM, lambda *_: cancel.set())
    runner = Calibrator(client, tuple(int(n) for n in args.frequencies.split(',')),
                        args.warmup, args.seconds, cancel=cancel,
                        progress=lambda event: print(json.dumps(event), flush=True), checkpoint=checkpoint)
    report = runner.run()
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    raise SystemExit(0 if report['status'] == 'completed' and report['restored'] else 1)


if __name__ == '__main__':
    main()
