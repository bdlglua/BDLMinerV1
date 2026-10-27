#!/usr/bin/env python3
"""Builds the web installer site (ESP Web Tools) from a release directory:

  <out>/index.html            board list + "Install" button (installer/index.html)
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
from build_output import staged_directory

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
]
CHIP_FAMILY = {"esp32": "ESP32", "esp32s2": "ESP32-S2", "esp32s3": "ESP32-S3", "esp32c3": "ESP32-C3"}


def build_installer(release, out):
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
            "name": f"BDLMiner V1 - {name}",
            "version": version,
            "new_install_prompt_erase": True,
            "builds": [{"chipFamily": CHIP_FAMILY[chips[env]], "parts": [{"path": image, "offset": 0}]}],
        }
        (Path(out) / f"manifest-{env}.json").write_text(json.dumps(manifest, indent=1), encoding="utf-8")
        listed.append({"env": env, "name": name})
    if not listed:
        raise ValueError("No supported boards with factory images in this release")
    page = (Path(ROOT) / "installer/index.html").read_text(encoding="utf-8")
    page = page.replace("__BOARDS__", json.dumps(listed)).replace("__VERSION__", version)
    (Path(out) / "index.html").write_text(page, encoding="utf-8")
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
