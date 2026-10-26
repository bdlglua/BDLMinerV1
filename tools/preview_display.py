#!/usr/bin/env python3
"""Test and preview the actual physical display renderer using LovyanGFX.

Install board dependencies once: pio pkg install -e m5stickc-plus2
Run checks only: python3 tools/preview_display.py --check
Export PNG previews (requires Pillow): python3 tools/preview_display.py
No board, SDL server, network, background generator or full-screen bitmap
assets are needed. Example readings are deliberately fictional.
"""
import argparse
from pathlib import Path
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent
PAGES = ("mining", "clock", "global", "network", "info", "graph", "records", "blocks")


def run(*args):
    subprocess.run([str(arg) for arg in args], cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="test without exporting PNG files")
    parser.add_argument("--output", type=Path, default=ROOT / "tools" / "preview")
    args = parser.parse_args()
    candidates = sorted((ROOT / ".pio" / "libdeps").glob("*/M5GFX/src"))
    preferred = ROOT / ".pio/libdeps/m5stickc-plus2/M5GFX/src"
    library = preferred if preferred.exists() else next(iter(candidates), None)
    if library is None:
        parser.error("M5GFX not installed; run pio pkg install -e m5stickc-plus2 first")
    build = ROOT / ".pio" / "host" / "display"
    build.mkdir(parents=True, exist_ok=True)
    compiler = os.environ.get("CXX", "g++")
    version = re.search(r'-DFW_VERSION=\\"([^"\\]+)\\"', (ROOT / 'platformio.ini').read_text()).group(1)
    flags = ["-std=c++17", "-O1", "-ffunction-sections", "-fdata-sections",
             "-DLGFX_LINUX_FB", "-DBDLMINER_DISPLAY_PREVIEW", "-Isrc",
             "-Itest/host/stubs/miner", "-I" + str(library), '-DFW_VERSION="' + version + '"']
    run(os.environ.get("CC", "gcc"), "-O1", "-ffunction-sections", "-fdata-sections", "-c",
        library / "lgfx/utility/lgfx_qrcode.c", "-o", build / "qrcode.o")
    run(compiler, *flags, "test/host/test_display.cpp", "src/ui_view.cpp", "src/ui_art.cpp", "src/ui_font.cpp", "src/i18n.cpp",
        library / "lgfx/v1/lgfx_v1.cpp", build / "qrcode.o", "-Wl,--gc-sections", "-pthread",
        "-o", build / "test_display")
    if args.check:
        run(build / "test_display")
        return
    from PIL import Image, ImageDraw
    raw = build / "raw"
    raw.mkdir(exist_ok=True)
    run(build / "test_display", raw)
    args.output.mkdir(parents=True, exist_ok=True)
    for path in sorted(raw.glob("*.ppm")):
        page, size, language = path.stem.split("_")
        prefix = PAGES[int(page[4:])] if page.startswith("page") else page
        name = prefix + "_" + size + ("_fr" if language == "fr" else "") + ".png"
        with Image.open(path) as image:
            image.save(args.output / name)
    for language in ("", "_fr"):
        sheet = Image.new("RGB", (1476, 1744), "#222936")
        draw = ImageDraw.Draw(sheet)
        for i, name in enumerate(PAGES):
            x, y = 12 + i % 2 * 732, 12 + i // 2 * 433
            label = "APERCU / DONNEES DE DEMO" if language else "PREVIEW / DEMO DATA"
            draw.text((x, y), f"{i + 1}/8 - {label}", fill="white")
            with Image.open(args.output / f"{name}_240x135{language}.png") as image:
                sheet.paste(image.resize((720, 405), Image.Resampling.LANCZOS), (x, y + 18))
        sheet.save(args.output / f"overview_240x135{language}.png")
    print("Previews use DEMO readings, not measurements from a connected miner:", args.output)


if __name__ == "__main__":
    main()
