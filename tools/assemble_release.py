#!/usr/bin/env python3
"""Validate parallel, unsigned board builds, then sign centrally and atomically."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
from build_output import staged_directory
from firmware_image import inspect_factory
import make_release
from release_version import Version


def assemble(inputs, output, version, key=None):
    version = Version(version).text
    expected = set(make_release.envs())
    boards = {}
    for metadata in sorted(Path(inputs).rglob('boards.json')):
        partial = json.loads(metadata.read_text())
        checksums = {}
        for line in metadata.with_name('SHA256SUMS.txt').read_text().splitlines():
            digest, name = line.split('  ')
            if name in checksums or Path(name).name != name:
                raise ValueError('Invalid or duplicated build checksum entry')
            checksums[name] = digest
        for env, chip in partial.items():
            if env not in expected or env in boards:
                raise ValueError('Unexpected or duplicated board: ' + env)
            app = metadata.with_name(env + '-firmware.bin')
            factory = metadata.with_name(env + '-factory.bin')
            for path in (app, factory):
                if path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest() != checksums.get(path.name):
                    raise ValueError('Build checksum mismatch: ' + path.name)
            inspect_factory(factory.read_bytes(), app.read_bytes(), env, version, chip)
            for path in (app, factory):
                shutil.copyfile(path, Path(output) / path.name)
            (Path(output) / ('ping-' + env + '.txt')).write_text('1\n')
            boards[env] = chip
    if set(boards) != expected:
        raise ValueError('Missing boards: ' + ', '.join(sorted(expected - set(boards))))
    (Path(output) / 'boards.json').write_text(json.dumps(boards, sort_keys=True, indent=1))
    # Compilation has finished; the release key is only used in this aggregate job.
    if key:
        for env in sorted(boards):
            make_release.sign(key, str(Path(output) / (env + '-firmware.bin')))
    make_release.write_checksums(str(output))
    make_release.write_update_manifest(str(output), version)
    return len(boards)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--inputs', type=Path, required=True)
    p.add_argument('--version', default=make_release.version())
    p.add_argument('--output', type=Path)
    p.add_argument('--unsigned', action='store_true', help='Validation-only preview; cannot serve OTA')
    args = p.parse_args()
    args.version = Version(args.version).text
    key = None if args.unsigned else make_release.signing_key()
    if not args.unsigned and not key:
        p.error('Central release signing key is missing')
    out = args.output or Path('release') / args.version
    if out.resolve() == args.inputs.resolve() or out.resolve() in args.inputs.resolve().parents:
        p.error('Output must not contain build inputs')
    with staged_directory(out) as staging:
        count = assemble(args.inputs, staging, args.version, key)
    print(f'Validated {count} board builds; {"signed release" if key else "unsigned preview"}: {out}')


if __name__ == '__main__':
    main()
