#!/usr/bin/env python3
"""Build stable installer plus an opt-in beta installer/compact discovery pointer."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile
from urllib.parse import quote
from build_output import staged_directory
from make_installer import build_installer
from release_version import Version, select_release


def validate_manifest(manifest, repository, tag, boards):
    if manifest.get('tag_name') != tag:
        raise ValueError('Channel manifest release mismatch')
    expected = {env + suffix for env in boards for suffix in ('-firmware.bin', '-firmware.bin.sig')}
    assets = manifest.get('assets', [])
    if len(assets) != len(expected) or {a.get('name') for a in assets} != expected:
        raise ValueError('Channel requires one firmware/signature pair per board')
    base = f'https://github.com/{repository}/releases/download/{quote(tag, safe="")}/'
    if any(a.get('browser_download_url') != base + a['name'] for a in assets):
        raise ValueError('Channel assets must belong to the selected repository release')


def build_channels(releases, fetch, output, repository):
    stable, beta = (select_release(releases, channel) for channel in ('stable', 'beta'))
    if stable is None:
        raise ValueError('A published stable release is required for the default installer')
    beta = beta or stable
    with tempfile.TemporaryDirectory(prefix='bdl-channels-') as temp:
        paths = {}
        for item in (stable, beta):
            tag = item.get('tag_name', item.get('tagName'))
            version = Version(tag).text
            if tag not in paths:
                folder = Path(temp) / version
                folder.mkdir()
                fetch(tag, folder)
                manifest = json.loads((folder / 'update.json').read_text())
                boards = json.loads((folder / 'boards.json').read_text())
                validate_manifest(manifest, repository, tag, boards)
                paths[tag] = folder
        stable_tag = stable.get('tag_name', stable.get('tagName'))
        beta_tag = beta.get('tag_name', beta.get('tagName'))
        build_installer(str(paths[stable_tag]), str(output))
        build_installer(str(paths[beta_tag]), str(Path(output) / 'beta'))
        pointers = Path(output) / 'updates'
        pointers.mkdir(exist_ok=True)
        for channel, tag in (('stable', stable_tag), ('beta', beta_tag)):
            (pointers / (channel + '.json')).write_bytes((paths[tag] / 'update.json').read_bytes())
    return stable_tag, beta_tag


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--repository', required=True)
    p.add_argument('--output', type=Path, default=Path('site'))
    args = p.parse_args()
    if not re.fullmatch(r'[\w.-]+/[\w.-]+', args.repository):
        p.error('Use owner/repository')
    listing = subprocess.run(['gh', 'api', '--paginate', f'repos/{args.repository}/releases?per_page=100',
                              '--jq', '.[] | {tag_name,draft,prerelease}'], check=True, capture_output=True, text=True)
    releases = [json.loads(line) for line in listing.stdout.splitlines() if line.strip()]
    def fetch(tag, folder):
        subprocess.run(['gh', 'release', 'download', tag, '--repo', args.repository, '--dir', str(folder),
                        '--pattern', '*-factory.bin', '--pattern', 'boards.json', '--pattern', 'update.json'], check=True)
    with staged_directory(args.output) as staged:
        stable, beta = build_channels(releases, fetch, staged, args.repository)
    print(f'Installer stable: {stable}; opt-in beta: {beta}')


if __name__ == '__main__':
    main()
