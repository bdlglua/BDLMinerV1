#!/usr/bin/env python3
"""Render the production Gamma 601 OLED at its real 128x32 resolution.

--check runs the native rendering checks without exporting PNGs.
All exported values are fictional, and require no connected board.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--output', type=Path, default=ROOT / '.pio/host/gamma-display/preview')
    args = parser.parse_args()
    libraries = sorted((ROOT / '.pio/libdeps').glob('*/M5GFX/src'))
    preferred = ROOT / '.pio/libdeps/bitaxe-gamma-601/M5GFX/src'
    library = preferred if preferred.exists() else next(iter(libraries), None)
    if library is None:
        parser.error('M5GFX is required; install a display board environment first')
    build = ROOT / '.pio/host/gamma-display'
    build.mkdir(parents=True, exist_ok=True)
    version = re.search(r'-DFW_VERSION=\\"([^"\\]+)\\"', (ROOT / 'platformio.ini').read_text()).group(1)
    flags = ['-std=c++17', '-O1', '-ffunction-sections', '-fdata-sections', '-DLGFX_LINUX_FB',
             '-DBOARD_GAMMA601', '-DBOARD_ASIC', '-DDISPLAY_LGFX_GAMMA601', '-DFW_VERSION="' + version + '"',
             '-Itest/host/stubs/gamma_display', '-Isrc', '-I' + str(library)]
    def run(*command):
        subprocess.run([str(part) for part in command], cwd=ROOT, check=True)
    run(os.environ.get('CC', 'gcc'), '-O1', '-ffunction-sections', '-fdata-sections', '-c',
        library / 'lgfx/utility/lgfx_qrcode.c', '-o', build / 'qrcode.o')
    run(os.environ.get('CXX', 'g++'), *flags, 'test/host/test_gamma_display.cpp',
        'src/core/work.cpp', 'src/core/sha256.cpp', *sorted((library / 'lgfx/v1').glob('*.cpp')),
        build / 'qrcode.o', '-Wl,--gc-sections', '-pthread', '-o', build / 'test')
    if args.check:
        run(build / 'test')
        return
    from PIL import Image, ImageDraw, ImageFont
    args.output.mkdir(parents=True, exist_ok=True)
    run(build / 'test', args.output)
    images = []
    for name in ('mining', 'power', 'pool', 'large-counts', 'fault', 'fault-detail', 'block', 'no-sensors', 'startup', 'wifi-setup', 'message'):
        image = Image.open(args.output / (name + '.ppm')).convert('RGB')
        image.save(args.output / (name + '-128x32.png'))
        images.append((name, image.copy()))
    sheet = Image.new('RGB', (800, 120 + len(images) * 200), '#10151d')
    draw = ImageDraw.Draw(sheet)
    draw.text((24, 20), 'BDLMiner / Bitaxe Gamma 601 - OLED 128 x 32', fill='white')
    draw.text((24, 44), 'PRODUCTION RENDERER - DEMO DATA - enlarged 5x', fill='#acb8c9')
    for i, (name, image) in enumerate(images):
        y = 90 + i * 200
        draw.text((24, y), name, fill='#acb8c9')
        sheet.paste(image.resize((640, 160), Image.Resampling.NEAREST), (136, y + 16))
    sheet.save(args.output / 'gamma601-overview.png')
    compact = Image.new('RGB', (560, 792), '#10151d')
    draw = ImageDraw.Draw(compact)
    label_font = ImageFont.load_default(size=18)
    draw.text((24, 16), 'Bitaxe Gamma 601 / OLED 128 x 32', fill='white', font=label_font)
    draw.text((24, 44), 'DEMO DATA / production renderer / enlarged 4x', fill='#acb8c9')
    by_name = dict(images)
    for i, (name, label) in enumerate((('mining', 'Mining'), ('power', 'Power and temperatures'),
                                        ('pool', 'Pool and records'), ('block', 'Block candidate alert'))):
        y = 78 + i * 172
        draw.text((24, y), label, fill='#acb8c9', font=label_font)
        compact.paste(by_name[name].resize((512, 128), Image.Resampling.NEAREST), (24, y + 24))
    compact.save(args.output / 'gamma601-compact.png')
    print('DEMO previews:', args.output)


if __name__ == '__main__':
    main()
