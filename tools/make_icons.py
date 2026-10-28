#!/usr/bin/env python3
"""Draw the dashboard's home-screen icons (web/icon-*.png): the orange coin of
the sidebar on the console background, inside the maskable safe zone. Run by
hand after changing the design; the PNGs are committed."""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
BACKGROUND, ACCENT, INK = (10, 13, 18), (247, 147, 26), (21, 16, 10)


def icon(size, font_path):
    scale = 4  # draw large, then downsample for smooth edges
    big = size * scale
    image = Image.new('RGB', (big, big), BACKGROUND)
    draw = ImageDraw.Draw(image)
    radius = big * 0.31  # the coin stays inside the 80 % safe circle of maskable icons
    centre = big / 2
    draw.ellipse((centre - radius, centre - radius, centre + radius, centre + radius), fill=ACCENT)
    # A bold B with the two bars of the bitcoin sign (few fonts carry U+20BF).
    font = ImageFont.truetype(font_path, int(radius * 1.15))
    left, top, right, bottom = draw.textbbox((centre, centre), 'B', font=font, anchor='mm')
    bar, overhang = big * 0.028, radius * 0.16
    for x in (left + (right - left) * 0.3, left + (right - left) * 0.6):
        draw.rectangle((x - bar / 2, top - overhang, x + bar / 2, top + bar), fill=INK)
        draw.rectangle((x - bar / 2, bottom - bar, x + bar / 2, bottom + overhang), fill=INK)
    draw.text((centre, centre), 'B', font=font, fill=INK, anchor='mm')
    image = image.resize((size, size), Image.LANCZOS)
    return image.quantize(colors=32, method=Image.Quantize.MEDIANCUT)  # a few kB in flash


def main():
    font_path = sys.argv[1] if len(sys.argv) > 1 else '/usr/share/fonts/TTF/DejaVuSans-Bold.ttf'
    for size in (192, 512):
        path = ROOT / 'web' / ('icon-%d.png' % size)
        icon(size, font_path).save(path, optimize=True)
        print(path, path.stat().st_size, 'bytes')


if __name__ == '__main__':
    main()
