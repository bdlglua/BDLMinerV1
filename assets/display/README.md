# Physical display illustrations

`illustrations.png` is original generated artwork for BDLMiner: a Bitcoin coin
with mining picks, a clock, a network globe and a small ASIC illustration.
The ASIC drawing is decorative artwork, not a photograph of supported hardware.
No NerdMiner image assets or logos are copied into this directory.

The physical display takes visual inspiration from
[NerdMinerV2](https://github.com/BitMaker-hub/NerdMiner_v2): illustrated branding,
an illustrated black instrument panel and yellow/cyan readouts. Text uses smooth
Sans glyphs in place of the earlier pixelated seven-segment/bitmap fonts.

Run `python3 tools/pack_display_art.py` with Pillow installed to encode the atlas
into `src/ui_art_data.h`. Only the four 128x128 RGB565 images are linked into display
firmwares (128 KiB total); the original PNG is not. The renderer area-filters
these images when reducing them for small panels. Normal builds need neither
Pillow nor a filesystem/image decoder on the device.

`DejaVuSans.ttf` and `DEJAVU-LICENSE.txt` provide the reproducible source and license
for the antialiased ASCII display font. `python3 tools/pack_display_fonts.py`
creates thirteen native sizes, with 4-bit coverage and run-length compression.
The generated tables live in flash and require no per-glyph allocation. The
original TTF is not included in the firmware or read by the device.
