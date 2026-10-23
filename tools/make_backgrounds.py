#!/usr/bin/env python3
"""Generates the BDLMiner V1 screen backgrounds.

Like NerdMinerV2, every screen is a full-screen bitmap with the panels already
drawn; the firmware paints the live values into the field rectangles defined
here, and the panel labels too (fields of role LABEL, with their English text),
so that they can be shown in the selected language. One set of images is
produced per panel resolution.

Outputs (committed, so building the firmware does not need Python):
  src/bg/bg_layout.h        field ids, roles and colors (shared)
  src/bg/bg_<W>x<H>.cpp     RGB565 pixels + field rectangles for one resolution
  tools/preview/*.png       previews

Usage: python3 tools/make_backgrounds.py
"""
import math
import os
import random

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "src", "bg")
PREVIEW_DIR = os.path.join(ROOT, "tools", "preview")

RESOLUTIONS = [(240, 135), (320, 170), (320, 240)]
DESIGN_W, DESIGN_H = 240, 135  # layout is written in these units, then scaled
SS = 4  # supersampling factor for anti-aliasing

ORANGE = (247, 147, 26)
ORANGE_DARK = (150, 82, 8)
BG_TOP = (20, 24, 34)
BG_BOTTOM = (5, 6, 10)
PANEL_TOP = (30, 36, 50)
PANEL_BOTTOM = (17, 21, 30)
BORDER = (52, 61, 80)
LABEL = (140, 150, 170)
WHITE = (236, 238, 242)
TRACE = (30, 37, 52)

# Default value colors per field role (RGB888, used by the firmware).
VALUE_WHITE = 0xFFFFFF
VALUE_ORANGE = 0xF7931A
VALUE_GREEN = 0x38D07A
VALUE_GREY = 0x8C96AA

FONT_CANDIDATES = {
    "black": ["/usr/share/fonts/noto/NotoSans-Black.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"],
    "bold": ["/usr/share/fonts/noto/NotoSans-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
             "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"],
}


def font(kind, px):
    for path in FONT_CANDIDATES[kind]:
        if os.path.exists(path):
            return ImageFont.truetype(path, max(1, int(round(px))))
    return ImageFont.load_default()


# ---------------------------------------------------------------------------
# Field registry: name -> (screen, role, align, color). Rectangles are added
# per resolution while drawing.
# ---------------------------------------------------------------------------

SCREENS = ["SPLASH", "MINING", "CLOCK", "GLOBAL", "NETWORK", "INFO"]
PAGES = len(SCREENS) - 1  # every screen but the splash has a page dot
ROLES = ["NUM", "TEXT", "WRAP", "LABEL"]
LABEL_COLOR = 0x8C96AA  # LABEL as RGB888
ALIGNS = ["LEFT", "CENTER", "RIGHT"]

FIELDS = [
    # name,            role,   align,    color
    ("HDR_BATTERY",     "TEXT", "RIGHT",  VALUE_GREY),
    ("HDR_STATUS",      "TEXT", "RIGHT",  VALUE_GREEN),
    ("SPLASH_INFO",     "TEXT", "CENTER", VALUE_GREY),
    ("MINING_HASHRATE", "NUM",  "CENTER", VALUE_ORANGE),
    ("MINING_UNIT",     "TEXT", "CENTER", VALUE_WHITE),
    ("MINING_SHARES",   "TEXT", "CENTER", VALUE_GREEN),
    ("MINING_BEST",     "TEXT", "CENTER", VALUE_WHITE),
    ("MINING_POOLDIFF", "TEXT", "CENTER", VALUE_WHITE),
    ("MINING_BLOCKS",   "NUM",  "CENTER", VALUE_WHITE),
    ("MINING_BITS32",   "NUM",  "CENTER", VALUE_WHITE),
    ("MINING_TEMP",     "TEXT", "CENTER", VALUE_WHITE),
    ("MINING_STATE",    "TEXT", "LEFT",   VALUE_GREY),
    ("MINING_UPTIME",   "TEXT", "RIGHT",  VALUE_GREY),
    ("CLOCK_TIME",      "NUM",  "CENTER", VALUE_WHITE),
    ("CLOCK_DATE",      "TEXT", "CENTER", VALUE_GREY),
    ("CLOCK_PRICE",     "TEXT", "CENTER", VALUE_ORANGE),
    ("CLOCK_HASHRATE",  "TEXT", "CENTER", VALUE_WHITE),
    ("CLOCK_HEIGHT",    "NUM",  "CENTER", VALUE_WHITE),
    ("CLOCK_FEE",       "TEXT", "CENTER", VALUE_WHITE),
    ("CLOCK_SHARES",    "TEXT", "CENTER", VALUE_GREEN),
    ("CLOCK_BEST",      "TEXT", "CENTER", VALUE_WHITE),
    ("CLOCK_STATE",     "TEXT", "LEFT",   VALUE_GREY),
    ("CLOCK_UPTIME",    "TEXT", "RIGHT",  VALUE_GREY),
    ("GLB_PRICE",       "NUM",  "CENTER", VALUE_ORANGE),
    ("GLB_SATS",        "TEXT", "CENTER", VALUE_GREY),
    ("GLB_NETHASH",     "TEXT", "CENTER", VALUE_WHITE),
    ("GLB_MEMPOOL",     "TEXT", "CENTER", VALUE_WHITE),
    ("GLB_FEES",        "TEXT", "CENTER", VALUE_WHITE),
    ("GLB_RETARGET",    "TEXT", "CENTER", VALUE_WHITE),
    ("GLB_EPOCH",       "TEXT", "LEFT",   VALUE_GREY),
    ("GLB_ODDS",        "TEXT", "RIGHT",  VALUE_GREY),
    ("NET_HEIGHT",      "NUM",  "CENTER", VALUE_ORANGE),
    ("NET_HALVING",     "TEXT", "CENTER", VALUE_GREY),
    ("NET_DIFF",        "TEXT", "CENTER", VALUE_WHITE),
    ("NET_HASHES",      "TEXT", "CENTER", VALUE_WHITE),
    ("NET_POOL",        "TEXT", "CENTER", VALUE_WHITE),
    ("NET_BESTEVER",    "TEXT", "CENTER", VALUE_WHITE),
    ("NET_IP",          "TEXT", "LEFT",   VALUE_GREY),
    ("NET_RSSI",        "TEXT", "RIGHT",  VALUE_GREY),
    ("INFO_BOARD",      "TEXT", "CENTER", VALUE_WHITE),
    ("INFO_FIRMWARE",   "TEXT", "CENTER", VALUE_ORANGE),
    ("INFO_ENGINES",    "TEXT", "CENTER", VALUE_WHITE),
    ("INFO_HEAP",       "TEXT", "CENTER", VALUE_WHITE),
    ("INFO_ADDRESS",    "WRAP", "CENTER", VALUE_WHITE),
]

# Texts the firmware writes (translated): panel labels ("L_" + the panel's
# first field), the splash tagline and the info screen's button help.
LABELS = [
    ("L_MINING_HASHRATE", "HASHRATE"), ("L_MINING_SHARES", "SHARES"), ("L_MINING_BEST", "BEST DIFF"),
    ("L_MINING_POOLDIFF", "POOL DIFF"), ("L_MINING_BLOCKS", "BLOCKS"), ("L_MINING_BITS32", "32-BIT"),
    ("L_MINING_TEMP", "TEMP"),
    ("L_CLOCK_TIME", "TIME"), ("L_CLOCK_PRICE", "BTC PRICE"), ("L_CLOCK_HASHRATE", "HASHRATE"),
    ("L_CLOCK_HEIGHT", "BLOCK"), ("L_CLOCK_FEE", "FEE sat/vB"), ("L_CLOCK_SHARES", "SHARES"),
    ("L_CLOCK_BEST", "BEST DIFF"),
    ("L_GLB_PRICE", "BITCOIN PRICE"), ("L_GLB_NETHASH", "NET HASHRATE"), ("L_GLB_MEMPOOL", "MEMPOOL TX"),
    ("L_GLB_FEES", "FEES sat/vB  FAST / 30M / 1H"), ("L_GLB_RETARGET", "NEXT RETARGET"),
    ("L_NET_HEIGHT", "BLOCK HEIGHT"), ("L_NET_DIFF", "NETWORK DIFF"), ("L_NET_HASHES", "TOTAL HASHES"),
    ("L_NET_POOL", "POOL"), ("L_NET_BESTEVER", "BEST EVER"),
    ("L_INFO_BOARD", "BOARD"), ("L_INFO_FIRMWARE", "FIRMWARE"), ("L_INFO_ENGINES", "MINING ENGINES"),
    ("L_INFO_HEAP", "FREE RAM"), ("L_INFO_ADDRESS", "PAYOUT ADDRESS"),
    ("SPLASH_TAGLINE", "BITCOIN  LOTTERY  MINER"),
    ("INFO_HELP", "A: next screen   B: brightness   hold A 5s: reset"),
]
LABEL_TEXT = dict(LABELS)
FIELDS += [(n, "LABEL", "LEFT" if n.startswith("L_") else "CENTER", LABEL_COLOR) for n, _ in LABELS]
FIELD_NAMES = [f[0] for f in FIELDS]


class Canvas:
    """Draws in design units (240x135) onto a supersampled image of W x H."""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.sx, self.sy = w / DESIGN_W, h / DESIGN_H
        self.s = min(self.sx, self.sy)  # uniform scale for fonts and radii
        self.img = Image.new("RGB", (w * SS, h * SS))
        self.d = ImageDraw.Draw(self.img)
        self.fields = {}
        self.screen = None  # set while a screen is drawn

    def X(self, x):
        return x * self.sx * SS

    def Y(self, y):
        return y * self.sy * SS

    def S(self, v):
        return v * self.s * SS

    def field(self, name, x, y, w, h):
        assert name in FIELD_NAMES, name
        x0, y0 = round(x * self.sx), round(y * self.sy)
        x1, y1 = round((x + w) * self.sx), round((y + h) * self.sy)
        self.fields[name] = (x0, y0, x1 - x0, y1 - y0, self.screen)

    def text(self, xy, s, size, color, kind="bold", anchor="la"):
        x, y = xy
        self.d.text((self.X(x), self.Y(y)), s, font=font(kind, self.S(size)), fill=color, anchor=anchor)

    def vgradient(self, box, top, bottom, radius=0):
        x0, y0, x1, y1 = [int(v) for v in box]
        grad = Image.new("RGB", (1, max(1, y1 - y0)))
        for i in range(y1 - y0):
            t = i / max(1, y1 - y0 - 1)
            grad.putpixel((0, i), tuple(int(a + (b - a) * t) for a, b in zip(top, bottom)))
        grad = grad.resize((max(1, x1 - x0), max(1, y1 - y0)))
        mask = Image.new("L", grad.size, 0)
        ImageDraw.Draw(mask).rounded_rectangle((0, 0, grad.size[0] - 1, grad.size[1] - 1), radius=radius, fill=255)
        self.img.paste(grad, (x0, y0), mask)

    # -- building blocks ----------------------------------------------------

    def background(self):
        self.vgradient((0, 0, self.w * SS, self.h * SS), BG_TOP, BG_BOTTOM)
        # Faint PCB traces, same pattern on every screen of a resolution.
        rnd = random.Random(21_000_000)
        lw = max(1, int(self.S(0.8)))
        for _ in range(26):
            x, y = rnd.uniform(0, DESIGN_W), rnd.uniform(0, DESIGN_H)
            pts = [(x, y)]
            for _ in range(rnd.randint(2, 4)):
                ang = rnd.choice([0, 45, 90, 135, 180, 225, 270, 315])
                ln = rnd.uniform(8, 40)
                x += ln * math.cos(math.radians(ang))
                y += ln * math.sin(math.radians(ang))
                pts.append((x, y))
            self.d.line([(self.X(px), self.Y(py)) for px, py in pts], fill=TRACE, width=lw, joint="curve")
            r = self.S(1.6)
            for px, py in (pts[0], pts[-1]):
                self.d.ellipse((self.X(px) - r, self.Y(py) - r, self.X(px) + r, self.Y(py) + r), outline=TRACE,
                               width=lw)

    def logo(self, cx, cy, r):
        """Bitcoin-style coin: orange disc with a tilted, double-barred B."""
        R = int(self.S(r))
        size = R * 2 + 4
        coin = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        cd = ImageDraw.Draw(coin)
        cd.ellipse((2, 2, size - 3, size - 3), fill=ORANGE)
        f = font("black", R * 1.45)
        cd.text((size / 2, size / 2 + R * 0.04), "B", font=f, fill=(255, 255, 255), anchor="mm")
        bw, bh = max(2, R * 0.11), R * 0.26
        bx = size / 2 - R * 0.13
        for dx in (0, R * 0.26):
            cd.rectangle((bx + dx - bw / 2, size / 2 - R * 0.62 - bh / 2, bx + dx + bw / 2, size / 2 - R * 0.62 + bh / 2),
                         fill=(255, 255, 255))
            cd.rectangle((bx + dx - bw / 2, size / 2 + R * 0.62 - bh / 2, bx + dx + bw / 2, size / 2 + R * 0.62 + bh / 2),
                         fill=(255, 255, 255))
        coin = coin.rotate(14, resample=Image.BICUBIC)
        self.img.paste(coin, (int(self.X(cx) - size / 2), int(self.Y(cy) - size / 2)), coin)

    def header(self, page):
        h = 19
        self.vgradient((0, 0, self.w * SS, self.Y(h)), (34, 28, 22), (16, 14, 14))
        self.d.rectangle((0, self.Y(h), self.w * SS, self.Y(h) + max(SS, self.S(1.2))), fill=ORANGE)
        self.logo(11, h / 2, 7)
        self.text((22, h / 2 + 0.5), "BDLMiner", 11.5, WHITE, "black", "lm")
        tw = self.d.textlength("BDLMiner", font=font("black", self.S(11.5))) / self.sx / SS
        self.text((22 + tw + 2.5, h / 2 + 0.5), "V1", 11.5, ORANGE, "black", "lm")
        # Page indicator.
        for i in range(PAGES):
            cx, cy, r = 110 + i * 5, h / 2, 1.6
            col = ORANGE if i == page else BORDER
            self.d.ellipse((self.X(cx) - self.S(r), self.Y(cy) - self.S(r), self.X(cx) + self.S(r),
                            self.Y(cy) + self.S(r)), fill=col)
        self.field("HDR_BATTERY", 133, 3, 30, 13)
        self.field("HDR_STATUS", 164, 3, 73, 13)

    def panel(self, x, y, w, h, label, accent=False):
        """`label`: the LABEL field (its text is written by the firmware)."""
        rad = self.S(4)
        box = (self.X(x), self.Y(y), self.X(x + w), self.Y(y + h))
        self.vgradient(box, PANEL_TOP, PANEL_BOTTOM, radius=int(rad))
        self.d.rounded_rectangle(box, radius=rad, outline=ORANGE_DARK if accent else BORDER,
                                 width=max(1, int(self.S(0.7))))
        if accent:
            self.d.rounded_rectangle((box[0] + rad, box[1], box[2] - rad, box[1] + self.S(1.6)), radius=self.S(0.8),
                                     fill=ORANGE)
        self.field(label, x + 4, y + 2, w - 8, 8)

    def value_panel(self, name, x, y, w, h, accent=False):
        """Panel with its label (field "L_" + name) on top and one value field below it."""
        self.panel(x, y, w, h, "L_" + name, accent)
        self.field(name, x + 3, y + 11, w - 6, h - 13)

    def footer_line(self):
        self.d.line((self.X(4), self.Y(121), self.X(236), self.Y(121)), fill=BORDER, width=max(1, int(self.S(0.6))))


# ---------------------------------------------------------------------------
# Screens
# ---------------------------------------------------------------------------

def draw_splash(c):
    c.background()
    c.logo(120, 46, 25)
    c.text((120, 84), "BDLMiner", 21, WHITE, "black", "mm")
    tw = c.d.textlength("BDLMiner", font=font("black", c.S(21))) / c.sx / SS
    c.text((120 + tw / 2 + 3, 84), "V1", 21, ORANGE, "black", "lm")
    c.field("SPLASH_TAGLINE", 20, 97, 200, 10)
    c.field("SPLASH_INFO", 4, 117, 232, 14)


def draw_mining(c):
    c.background()
    c.header(0)
    c.panel(4, 24, 146, 67, "L_MINING_HASHRATE", accent=True)
    c.field("MINING_HASHRATE", 8, 35, 138, 38)
    c.field("MINING_UNIT", 8, 74, 138, 15)
    c.value_panel("MINING_SHARES", 154, 24, 82, 33)
    c.value_panel("MINING_BEST", 154, 59, 82, 32)
    w = (232 - 3 * 4) / 4
    for i, name in enumerate(["MINING_POOLDIFF", "MINING_BLOCKS", "MINING_BITS32", "MINING_TEMP"]):
        c.value_panel(name, 4 + i * (w + 4), 94, w, 25)
    c.footer_line()
    c.field("MINING_STATE", 4, 122, 150, 12)
    c.field("MINING_UPTIME", 150, 122, 86, 12)


def draw_clock(c):
    c.background()
    c.header(1)
    c.panel(4, 24, 146, 67, "L_CLOCK_TIME", accent=True)
    c.field("CLOCK_TIME", 8, 35, 138, 38)
    c.field("CLOCK_DATE", 8, 74, 138, 15)
    c.value_panel("CLOCK_PRICE", 154, 24, 82, 33)
    c.value_panel("CLOCK_HASHRATE", 154, 59, 82, 32)
    w = (232 - 3 * 4) / 4
    for i, name in enumerate(["CLOCK_HEIGHT", "CLOCK_FEE", "CLOCK_SHARES", "CLOCK_BEST"]):
        c.value_panel(name, 4 + i * (w + 4), 94, w, 25)
    c.footer_line()
    c.field("CLOCK_STATE", 4, 122, 150, 12)
    c.field("CLOCK_UPTIME", 150, 122, 86, 12)


def draw_global(c):
    c.background()
    c.header(2)
    c.panel(4, 24, 146, 67, "L_GLB_PRICE", accent=True)
    c.field("GLB_PRICE", 8, 35, 138, 38)
    c.field("GLB_SATS", 8, 74, 138, 15)
    c.value_panel("GLB_NETHASH", 154, 24, 82, 33)
    c.value_panel("GLB_MEMPOOL", 154, 59, 82, 32)
    c.value_panel("GLB_FEES", 4, 94, 146, 25)
    c.value_panel("GLB_RETARGET", 154, 94, 82, 25)
    c.footer_line()
    c.field("GLB_EPOCH", 4, 122, 116, 12)
    c.field("GLB_ODDS", 120, 122, 116, 12)


def draw_network(c):
    c.background()
    c.header(3)
    c.panel(4, 24, 146, 67, "L_NET_HEIGHT", accent=True)
    c.field("NET_HEIGHT", 8, 35, 138, 38)
    c.field("NET_HALVING", 8, 74, 138, 15)
    c.value_panel("NET_DIFF", 154, 24, 82, 33)
    c.value_panel("NET_HASHES", 154, 59, 82, 32)
    c.value_panel("NET_POOL", 4, 94, 146, 25)
    c.value_panel("NET_BESTEVER", 154, 94, 82, 25)
    c.footer_line()
    c.field("NET_IP", 4, 122, 150, 12)
    c.field("NET_RSSI", 150, 122, 86, 12)


def draw_info(c):
    c.background()
    c.header(4)
    c.value_panel("INFO_BOARD", 4, 24, 146, 33, accent=True)
    c.value_panel("INFO_FIRMWARE", 154, 24, 82, 33)
    c.value_panel("INFO_ENGINES", 4, 59, 146, 32)
    c.value_panel("INFO_HEAP", 154, 59, 82, 32)
    c.panel(4, 94, 232, 26, "L_INFO_ADDRESS")
    c.field("INFO_ADDRESS", 8, 103, 224, 16)
    c.footer_line()
    c.field("INFO_HELP", 4, 123, 232, 11)


DRAW = {"SPLASH": draw_splash, "MINING": draw_mining, "CLOCK": draw_clock, "GLOBAL": draw_global,
        "NETWORK": draw_network, "INFO": draw_info}


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

BAYER4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], dtype=np.float32) / 16.0


def to_rgb565(img):
    """Ordered dithering avoids banding of the dark gradients in 16-bit."""
    a = np.asarray(img, dtype=np.float32)
    h, w, _ = a.shape
    t = np.tile(BAYER4, (h // 4 + 1, w // 4 + 1))[:h, :w] - 0.5
    r = np.clip(np.floor(a[:, :, 0] / 255 * 31 + 0.5 + t), 0, 31).astype(np.uint16)
    g = np.clip(np.floor(a[:, :, 1] / 255 * 63 + 0.5 + t), 0, 63).astype(np.uint16)
    b = np.clip(np.floor(a[:, :, 2] / 255 * 31 + 0.5 + t), 0, 31).astype(np.uint16)
    px = (r << 11) | (g << 5) | b
    preview = np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], axis=2).astype(np.uint8)
    return px.flatten(), Image.fromarray(preview)


FIELD_SCREEN = {}  # filled while drawing the first resolution


def write_layout():
    lines = [
        "// Generated by tools/make_backgrounds.py - do not edit.",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "namespace bg {",
        "",
        "enum Screen : uint8_t { " + ", ".join(SCREENS) + ", SCREEN_COUNT };",
        "enum Role : uint8_t { " + ", ".join("ROLE_" + r for r in ROLES) + " };",
        "enum Align : uint8_t { " + ", ".join("ALIGN_" + a for a in ALIGNS) + " };",
        "",
        "enum Field : uint8_t {",
    ]
    lines += [f"    {n}," for n in FIELD_NAMES]
    lines += ["    FIELD_COUNT,", "};", ""]
    lines.append("constexpr Role kRole[FIELD_COUNT] = {" + ", ".join("ROLE_" + f[1] for f in FIELDS) + "};")
    lines.append("constexpr Align kAlign[FIELD_COUNT] = {" + ", ".join("ALIGN_" + f[2] for f in FIELDS) + "};")
    lines.append("constexpr uint32_t kColor[FIELD_COUNT] = {" + ", ".join(f"0x{f[3]:06X}" for f in FIELDS) + "};")
    lines.append("// English text of the LABEL fields (nullptr for the others).")
    lines.append("constexpr const char *kLabel[FIELD_COUNT] = {" +
                 ", ".join(f'"{LABEL_TEXT[n]}"' if n in LABEL_TEXT else "nullptr" for n in FIELD_NAMES) + "};")
    lines.append("// Screen of each field; SCREEN_COUNT for the header fields, on every screen.")
    lines.append("constexpr Screen kScreen[FIELD_COUNT] = {" + ", ".join(FIELD_SCREEN[n] for n in FIELD_NAMES) + "};")
    lines += ["", "}  // namespace bg", ""]
    with open(os.path.join(OUT_DIR, "bg_layout.h"), "w") as fh:
        fh.write("\n".join(lines))


def write_resolution(w, h):
    tag = f"{w}x{h}"
    fields = {}
    arrays = []
    for screen in SCREENS:
        c = Canvas(w, h)
        c.screen = screen
        DRAW[screen](c)
        img = c.img.resize((w, h), Image.LANCZOS)
        px, preview = to_rgb565(img)
        preview.save(os.path.join(PREVIEW_DIR, f"{screen.lower()}_{tag}.png"))
        fields.update(c.fields)
        body = ",\n".join(
            "    " + ",".join(f"0x{v:04X}" for v in px[i:i + 16]) for i in range(0, len(px), 16))
        arrays.append(f"static const uint16_t px_{screen.lower()}[{w * h}] = {{\n{body}\n}};\n")

    missing = [n for n in FIELD_NAMES if n not in fields]
    assert not missing, missing
    for n in FIELD_NAMES:
        FIELD_SCREEN.setdefault(n, "SCREEN_COUNT" if n.startswith("HDR_") else fields[n][4])
    rects = "\n".join(f"    {{{x}, {y}, {fw}, {fh}}},  // {n}" for n in FIELD_NAMES for (x, y, fw, fh, _) in [fields[n]])
    src = f"""// Generated by tools/make_backgrounds.py - do not edit.
#include "bg.h"

#if BG_HAS_{tag}

namespace bg {{
namespace {{

{chr(10).join(arrays)}
const Rect rects[FIELD_COUNT] = {{
{rects}
}};

}}  // namespace

extern const Set set_{tag} = {{{w}, {h}, {{{", ".join("px_" + s.lower() for s in SCREENS)}}}, rects}};

}}  // namespace bg

#endif
"""
    with open(os.path.join(OUT_DIR, f"bg_{tag}.cpp"), "w") as fh:
        fh.write(src)
    print(f"{tag}: {len(SCREENS)} screens, {len(SCREENS) * w * h * 2 // 1024} KiB")


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    os.makedirs(PREVIEW_DIR, exist_ok=True)
    for w, h in RESOLUTIONS:
        write_resolution(w, h)
    write_layout()  # after drawing: needs the screen of every field


if __name__ == "__main__":
    main()
