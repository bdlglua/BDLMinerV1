# Physical display fonts

`DejaVuSans.ttf`, `DejaVuSans-Bold.ttf` and `DEJAVU-LICENSE.txt` are the reproducible
source and license of the antialiased display font. `python3 tools/pack_display_fonts.py`
(Pillow required) bakes ten text sizes (8-24 px) and eighteen numeral sizes (8-80 px,
digits, separators and SI prefixes) into `src/ui_font_data.h`, with 4-bit coverage and
run-length compression. Labels up to 12 pixels use the bold face; numeral faces are
fitted to cap height rather than a full ASCII line box, so the big values of the
"one big number" screens fill their field.

The generated tables live in flash and need no per-glyph allocation. The original TTF
files are not included in the firmware or read by the device.
