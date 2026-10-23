#!/usr/bin/env python3
"""Builds the web installer site (ESP Web Tools) from a release directory:

  <out>/index.html            board list + "Install" button (installer/index.html)
  <out>/manifest-<env>.json   one ESP Web Tools manifest per board
  <out>/<env>-factory.bin     the images it flashes at 0x0

    python3 tools/make_installer.py release/<version> <out>

The GitHub workflow publishes <out> on GitHub Pages: the browser can only
flash files from the same site (release downloads are not CORS-enabled).
"""
import json
import os
import shutil
import sys

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
]
CHIP_FAMILY = {"esp32": "ESP32", "esp32s2": "ESP32-S2", "esp32s3": "ESP32-S3", "esp32c3": "ESP32-C3"}


def main():
    release, out = sys.argv[1], sys.argv[2]
    version = os.path.basename(os.path.normpath(release))
    chips = json.load(open(os.path.join(release, "boards.json")))
    os.makedirs(out, exist_ok=True)
    listed = []
    for env, name in BOARDS:
        image = f"{env}-factory.bin"
        if env not in chips or not os.path.exists(os.path.join(release, image)):
            continue
        shutil.copyfile(os.path.join(release, image), os.path.join(out, image))
        manifest = {
            "name": f"BDLMiner V1 - {name}",
            "version": version,
            "new_install_prompt_erase": True,
            "builds": [{"chipFamily": CHIP_FAMILY[chips[env]], "parts": [{"path": image, "offset": 0}]}],
        }
        json.dump(manifest, open(os.path.join(out, f"manifest-{env}.json"), "w"), indent=1)
        listed.append({"env": env, "name": name})
    page = open(os.path.join(ROOT, "installer", "index.html")).read()
    page = page.replace("__BOARDS__", json.dumps(listed)).replace("__VERSION__", version)
    open(os.path.join(out, "index.html"), "w").write(page)
    print(f"installer for {version}: {len(listed)} boards in {out}")


if __name__ == "__main__":
    main()
