#!/usr/bin/env python3
"""Builds every board and writes the release binaries to release/<version>/:

  <env>-factory.bin   bootloader + partition table + firmware, to flash at 0x0
                      (esptool, or a web flasher such as https://espressif.github.io/esptool-js/)
  <env>-firmware.bin  the firmware alone, for updates from the web dashboard
  SHA256SUMS.txt

    python3 tools/make_release.py [env ...]
"""
import hashlib
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIO_HOME = os.path.expanduser("~/.platformio")
PYTHON = os.path.join(PIO_HOME, "penv", "bin", "python")
ESPTOOL = os.path.join(PIO_HOME, "packages", "tool-esptoolpy", "esptool.py")


def envs():
    text = open(os.path.join(ROOT, "platformio.ini")).read()
    return re.findall(r"^\[env:([^\]]+)\]", text, re.M)


def version():
    text = open(os.path.join(ROOT, "platformio.ini")).read()
    return re.search(r'-DFW_VERSION=\\"([^"\\]+)\\"', text).group(1)


def run(*args):
    return subprocess.run(args, cwd=ROOT, check=True, capture_output=True, text=True).stdout


def flash_layout(env):
    """Chip, esptool flash settings and the images (offset, path), as `pio run -t upload` uses them."""
    dump = run("pio", "run", "-e", env, "-t", "envdump")
    get = lambda key: re.search(r"'%s': '([^']*)'" % key, dump).group(1)
    mcu = get("BOARD_MCU")
    mode = get("BOARD_FLASH_MODE")
    mode = "dio" if mode in ("qio", "qout") else mode  # what PlatformIO passes to esptool
    freq = "%dm" % (int(get("BOARD_F_FLASH").rstrip("L")) // 1000000)
    size = re.search(r"'--flash_size',\s*'([^']+)'", dump).group(1)
    extra = re.search(r"'FLASH_EXTRA_IMAGES': \[(.*?)\]", dump, re.S).group(1)
    images = re.findall(r"\(\s*'(0x[0-9a-fA-F]+)',\s*'([^']+)'\)", extra)
    images.append((get("ESP32_APP_OFFSET"), os.path.join(ROOT, ".pio", "build", env, "firmware.bin")))
    return mcu, mode, freq, size, images


def main():
    targets = sys.argv[1:] or envs()
    out = os.path.join(ROOT, "release", version())
    os.makedirs(out, exist_ok=True)
    sums = []
    for env in targets:
        print(f"[{env}] building", flush=True)
        run("pio", "run", "-e", env)
        mcu, mode, freq, size, images = flash_layout(env)
        factory = os.path.join(out, f"{env}-factory.bin")
        args = [PYTHON, ESPTOOL, "--chip", mcu, "merge_bin", "-o", factory,
                "--flash_mode", mode, "--flash_freq", freq, "--flash_size", size]
        for offset, path in images:
            args += [offset, path]
        subprocess.run(args, check=True, capture_output=True)
        firmware = os.path.join(out, f"{env}-firmware.bin")
        shutil.copyfile(images[-1][1], firmware)
        for path in (factory, firmware):
            sums.append(f"{hashlib.sha256(open(path, 'rb').read()).hexdigest()}  {os.path.basename(path)}")
        print(f"[{env}] {mcu}, {size} flash, {mode} {freq}: "
              f"{os.path.getsize(factory)} + {os.path.getsize(firmware)} bytes", flush=True)
    open(os.path.join(out, "SHA256SUMS.txt"), "w").write("\n".join(sums) + "\n")
    print(f"written to {out}")


if __name__ == "__main__":
    main()
