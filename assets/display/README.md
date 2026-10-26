# Physical display illustrations

`illustrations.png` is original generated artwork for BDLMiner: a Bitcoin coin
with mining picks, a clock, a network globe and a small ASIC illustration.
The ASIC drawing is decorative artwork, not a photograph of supported hardware.
No NerdMiner image assets or logos are copied into this directory.

The physical display takes visual inspiration from
[NerdMinerV2](https://github.com/BitMaker-hub/NerdMiner_v2): illustrated branding,
digital numerals, a black instrument panel and yellow/cyan readouts.

Run `python3 tools/pack_display_art.py` with Pillow installed to encode the atlas
into `src/ui_art_data.h`. Only the four 64x64 RGB565 images are linked into display
firmwares (32 KiB total); the original PNG is not. Normal builds need neither
Pillow nor a filesystem/image decoder on the device.
