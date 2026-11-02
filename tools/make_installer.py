#!/usr/bin/env python3
"""Builds the BDLMiner site with its web installer (ESP Web Tools) from a
release directory:

  <out>/index.html            the site: presentation, boards, "Install" button,
                              live statistics (installer/index.html)
  <out>/assets/               its images (installer/assets/)
  <out>/manifest-<env>.json   one ESP Web Tools manifest per board
  <out>/<env>-factory.bin     the images it flashes at 0x0

    python3 tools/make_installer.py release/<version> <out>

The GitHub workflow publishes <out> on GitHub Pages: the browser can only
flash files from the same site (release downloads are not CORS-enabled).
"""
import argparse
import json
import os
from pathlib import Path
import shutil
from urllib.parse import urlsplit
from build_output import staged_directory
from make_site_demo import build_demos
from make_site_pages import build_pages

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Shown in the board list, in this order.
BOARDS = [
    ("m5stickc-plus2", "M5StickC Plus2"),
    ("m5stack-esp32", "M5Stack ESP32 (Core, Core2, StickC Plus, Atom...)"),
    ("m5stack-s3", "M5Stack ESP32-S3 (AtomS3, CoreS3, Cardputer...)"),
    ("lilygo-t-display", "LilyGO T-Display"),
    ("lilygo-t-display-s3", "LilyGO T-Display-S3"),
    ("esp32-2432s028r", "ESP32-2432S028R (Cheap Yellow Display)"),
    ("esp32-headless", "ESP32 without a screen"),
    ("esp32s3-headless", "ESP32-S3 without a screen"),
    ("esp32s2-headless", "ESP32-S2 without a screen"),
    ("esp32c3-headless", "ESP32-C3 without a screen"),
    ("bitaxe-gamma-601", "Bitaxe Gamma 601 (experimental; 5V barrel supply + fan required)"),
    ("nerdnos", "NerdNOS: BM1397 board on a T-Display-S3 (experimental; 5V 2A USB)"),
]
CHIP_FAMILY = {"esp32": "ESP32", "esp32s2": "ESP32-S2", "esp32s3": "ESP32-S3", "esp32c3": "ESP32-C3"}


def site_content(version, channel='stable', channels=None):
    content = json.loads((Path(ROOT) / 'installer/content.json').read_text(encoding='utf-8'))
    for pool, destination in content.get('pool_links', {}).items():
        url = urlsplit(destination)
        if (pool in ('other', 'unknown') or pool not in content.get('pool_names', {}) or
                url.scheme != 'https' or not url.hostname or url.username or url.password or
                url.query or url.fragment or not (url.hostname == pool or url.hostname.endswith('.' + pool))):
            raise ValueError('Pool websites must be public HTTPS links for their recognised group')
    support = content['support']
    for link in support['links']:
        url = urlsplit(link['url'])
        if url.scheme != 'https' or not url.hostname or url.username or url.password:
            raise ValueError('Donation links must use HTTPS without credentials')
    for key in ('bitcoin', 'lightning'):
        value = support.get(key, '')
        if any(c.isspace() or c in '<>"\'\\' for c in value):
            raise ValueError('Invalid donation destination')
    for entry in content['gallery']:
        image = Path(entry['image'])
        if image.is_absolute() or '..' in image.parts or image.parts[:1] != ('assets',):
            raise ValueError('Gallery images must be local site assets')
        url = urlsplit(entry['url'])
        if url.scheme != 'https' or url.hostname not in ('reddit.com', 'www.reddit.com', 'old.reddit.com', 'discord.com', 'discord.gg'):
            raise ValueError('Gallery entries must link to Reddit or Discord')
        if entry.get('permission') is not True:
            raise ValueError('Gallery photos require contributor permission')
        if not (Path(ROOT) / 'installer' / image).is_file():
            raise ValueError('Missing gallery image')
    content.update(version=version, channel=channel, channels=channels or {
        'stable': {'version': version, 'published_at': None},
        'beta': {'version': version, 'published_at': None},
    })
    return content


def build_installer(release, out, channel='stable', channels=None):
    version = os.path.basename(os.path.normpath(release))
    chips = json.loads((Path(release) / "boards.json").read_text(encoding="utf-8"))
    os.makedirs(out, exist_ok=True)
    listed = []
    for env, name in BOARDS:
        image = f"{env}-factory.bin"
        if env not in chips:
            continue
        if chips[env] not in CHIP_FAMILY:
            raise ValueError(f"Unsupported chip for {env}: {chips[env]}")
        if not os.path.isfile(os.path.join(release, image)):
            raise ValueError(f"Missing factory image: {image}")
        shutil.copyfile(os.path.join(release, image), os.path.join(out, image))
        manifest = {
            "name": f"BDLMiner V2 - {name}",
            "version": version,
            "new_install_prompt_erase": True,
            "builds": [{"chipFamily": CHIP_FAMILY[chips[env]], "parts": [{"path": image, "offset": 0}]}],
        }
        (Path(out) / f"manifest-{env}.json").write_text(json.dumps(manifest, indent=1), encoding="utf-8")
        listed.append({"env": env, "name": name})
    if not listed:
        raise ValueError("No supported boards with factory images in this release")
    build_pages(ROOT, out, version, listed, site_content(version, channel, channels))
    assets = Path(ROOT) / "installer/assets"
    if assets.is_dir():
        shutil.copytree(assets, Path(out) / "assets", dirs_exist_ok=True)
    build_demos(ROOT, out, version)
    return version, len(listed)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("release", type=Path)
    parser.add_argument("out", type=Path)
    args = parser.parse_args()
    release, out = args.release.resolve(), args.out.resolve()
    # Replacing a source directory (or its parent) would destroy the inputs.
    if out == release or out in release.parents:
        parser.error("installer output must not contain the source release")
    with staged_directory(out) as staged:
        version, count = build_installer(str(release), str(staged))
    print(f"installer for {version}: {count} boards in {out}")


if __name__ == "__main__":
    main()
