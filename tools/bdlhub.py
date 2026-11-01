#!/usr/bin/env python3
"""BDLMiner V2 fleet dashboard. No pip packages required (Python 3.10+)."""
import argparse
from pathlib import Path
from bdlhost.client import load_config
from bdlhost.hub import serve


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config', type=Path, required=True)
    p.add_argument('--database', type=Path, default=Path('bdlhub.sqlite'))
    p.add_argument('--bind', default='127.0.0.1')
    p.add_argument('--port', type=int, default=8787)
    args = p.parse_args()
    config, devices = load_config(args.config)
    serve(config, devices, args.database, args.bind, args.port)


if __name__ == '__main__':
    main()
