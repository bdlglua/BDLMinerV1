#!/usr/bin/env python3
"""Builds every board and writes the release binaries to release/<version>/:

  <env>-factory.bin   bootloader + partition table + firmware, to flash at 0x0
                      (esptool, or a web flasher such as https://espressif.github.io/esptool-js/)
  <env>-firmware.bin  the firmware alone, for updates from the web dashboard
  <env>-firmware.bin.sig  its ECDSA P-256 signature, which updates from GitHub
                      require (key: keys/release-signing-key.pem, or the file
                      named by RELEASE_SIGNING_KEY_FILE; skipped without one)
  ping-<env>.txt      2 bytes each miner downloads once a day: the download
                      counts are the anonymous usage count (tools/miner_stats.py)
  SHA256SUMS.txt
  boards.json         the chip of each board, for tools/make_installer.py
  update.json         compact discovery metadata for GitHub firmware updates

    python3 tools/make_release.py [--version X.Y.Z] [env ...]

--version first sets FW_VERSION in platformio.ini (the release workflow takes
it from the git tag, so that a v1.0.1 release really contains firmware 1.0.1).
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from urllib.parse import quote

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIO_HOME = os.path.expanduser(os.environ.get("PLATFORMIO_CORE_DIR", "~/.platformio"))
PYTHON = os.path.join(PIO_HOME, "penv", "bin", "python")
if not os.path.exists(PYTHON):  # PlatformIO installed with pip (CI)
    PYTHON = sys.executable
ESPTOOL = os.path.join(PIO_HOME, "packages", "tool-esptoolpy", "esptool.py")


def envs():
    text = open(os.path.join(ROOT, "platformio.ini")).read()
    return re.findall(r"^\[env:([^\]]+)\]", text, re.M)


def version():
    text = open(os.path.join(ROOT, "platformio.ini")).read()
    return re.search(r'-DFW_VERSION=\\"([^"\\]+)\\"', text).group(1)


def checked(args, **kwargs):
    try:
        return subprocess.run(args, check=True, capture_output=True, **kwargs)
    except subprocess.CalledProcessError as error:
        # A captured stderr must not disappear behind only "exit status 2".
        for output in (error.stdout, error.stderr):
            if output:
                print(output.decode(errors="replace") if isinstance(output, bytes) else output, file=sys.stderr)
        raise


def run(*args):
    return checked(args, cwd=ROOT, text=True).stdout


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


def signing_key():
    path = os.environ.get("RELEASE_SIGNING_KEY_FILE") or os.path.join(ROOT, "keys", "release-signing-key.pem")
    return path if os.path.exists(path) else None


def sign(key, path):
    """Writes path.sig and checks it against the public key built into the firmware."""
    subprocess.run(["openssl", "dgst", "-sha256", "-sign", key, "-out", path + ".sig", path], check=True)
    header = open(os.path.join(ROOT, "src", "update_key.h")).read()
    lines = re.findall(r'"([^"]*)\\n"', header)  # the PEM, one line per string literal
    pem = "\n".join(lines)
    pub = path + ".pub.pem"
    open(pub, "w").write(pem + "\n")
    try:
        subprocess.run(["openssl", "dgst", "-sha256", "-verify", pub, "-signature", path + ".sig", path],
                       check=True, capture_output=True)
    finally:
        os.remove(pub)


def set_version(v):
    path = os.path.join(ROOT, "platformio.ini")
    text = open(path).read()
    new = re.sub(r'(-DFW_VERSION=\\")[^"\\]+(\\")', lambda m: m.group(1) + v + m.group(2), text)
    if new == text and f'-DFW_VERSION=\\"{v}\\"' not in text:
        sys.exit("FW_VERSION not found in platformio.ini")
    open(path, "w").write(new)


def write_checksums(out):
    """Include previous boards too when rebuilding only part of a release."""
    sums = []
    for name in sorted(os.listdir(out)):
        if not name.endswith(("-factory.bin", "-firmware.bin")):
            continue
        with open(os.path.join(out, name), "rb") as image:
            digest = hashlib.sha256(image.read()).hexdigest()
        sums.append(f"{digest}  {name}")
    with open(os.path.join(out, "SHA256SUMS.txt"), "w") as output:
        output.write("\n".join(sums) + "\n")


def write_update_manifest(out, release_version):
    """Small discovery document, with the same asset fields as the releases API."""
    repository = os.environ.get("GITHUB_REPOSITORY", "bdlglua/BDLMinerV1")
    base = f"https://github.com/{repository}/releases/download/v{quote(release_version, safe='')}/"
    assets = []
    for name in sorted(os.listdir(out)):
        if name.endswith("-firmware.bin") and os.path.isfile(os.path.join(out, name + ".sig")):
            for asset in (name, name + ".sig"):
                assets.append({"name": asset, "browser_download_url": base + quote(asset, safe='')})
    with open(os.path.join(out, "update.json"), "w") as output:
        json.dump({"tag_name": "v" + release_version, "assets": assets}, output, separators=(",", ":"))


def main():
    args = sys.argv[1:]
    if args[:1] == ["--version"]:
        set_version(args[1])
        args = args[2:]
    targets = args or envs()
    out = os.path.join(ROOT, "release", version())
    os.makedirs(out, exist_ok=True)
    boards = {}
    key = signing_key()
    if not key:
        print("no signing key: the firmware is not signed, GitHub updates will refuse it")
    for env in targets:
        print(f"[{env}] building", flush=True)
        # Discover the layout (and run pre-build generators) before compiling.
        # A fresh envdump must never invalidate/remove images after the build
        # that the merge command is about to consume.
        mcu, mode, freq, size, images = flash_layout(env)
        run("pio", "run", "-e", env)
        missing = [path for _, path in images if not os.path.isfile(path)]
        if missing:
            sys.exit("Build did not produce required flash images: " + ", ".join(missing))
        factory = os.path.join(out, f"{env}-factory.bin")
        args = [PYTHON, ESPTOOL, "--chip", mcu, "merge_bin", "-o", factory,
                "--flash_mode", mode, "--flash_freq", freq, "--flash_size", size]
        for offset, path in images:
            args += [offset, path]
        checked(args)
        firmware = os.path.join(out, f"{env}-firmware.bin")
        # A previous signed build may already exist in this release folder.
        # Never leave its signature attached to the newly copied firmware.
        if os.path.exists(firmware + ".sig"):
            os.remove(firmware + ".sig")
        shutil.copyfile(images[-1][1], firmware)
        if key:
            sign(key, firmware)
        boards[env] = mcu
        with open(os.path.join(out, f"ping-{env}.txt"), "w") as ping:
            ping.write("1\n")
        print(f"[{env}] {mcu}, {size} flash, {mode} {freq}: "
              f"{os.path.getsize(factory)} + {os.path.getsize(firmware)} bytes", flush=True)
    write_checksums(out)
    write_update_manifest(out, version())
    path = os.path.join(out, "boards.json")
    old = {}
    if os.path.exists(path):
        with open(path) as previous:
            old = json.load(previous)
    with open(path, "w") as output:
        json.dump({**old, **boards}, output, indent=1)
    print(f"written to {out}")


if __name__ == "__main__":
    main()
