# Gamma 601 OLED layout review — 1 November 2026

Unreleased. Starting point: `e588f7fe1ea3e5c3446ed017d0f2d5dd17f106e6`.

The board definition and the upstream reference agree on an SSD1306 **128×32**
OLED. This interface is separate from the color-screen `ui_view` renderer.
The earlier color-display regression suite therefore did not exercise it.

The actual Gamma renderer and LovyanGFX Font0 were executed against a one-bit
128×32 native sprite. Font0 measures **6×8 pixels**, so four lines fit vertically
and 21 characters require 126 pixels. Normal pages had no overlaps. Confirmed
problems were silent truncation of large accepted/rejected counters and long
status messages; externally supplied newlines could also escape their row.

Corrections:

- Explicitly select Font0 and retain four eight-pixel rows at 0°/180° rotation.
  Measure text against the 126-pixel inner width, with a one-pixel side margin.
- Use concise `A:`/`R:` labels and SI notation from one million; both counters
  remain visible through `UINT32_MAX`. Exact totals remain in the web dashboard.
- Shorten the power line to `Vin ...V Vc ...V`; unavailable readings show `--`.
- Page complete long messages every three seconds, preferring word boundaries,
  preserving UTF-8 boundaries and restarting when the text changes. Replace
  control characters with spaces so they cannot draw into adjacent rows.
- Center the block candidate alert and remove decorative stars. Its wording,
  proof height and dismissal behavior are retained.
- Complete text formatting before taking the shared OLED/ASIC I2C bus.

`tools/preview_gamma_display.py` compiles the production `ui_gamma.cpp` directly,
with actual LovyanGFX fonts/rendering and mocked readings/time/WiFi. Its tests
cover each row's pixel bounds and side margins, both supported rotations, font
reset, huge counters, long fault messages and SSIDs, UTF-8, control characters,
timer wrap, missing sensors, setup, splash and candidate alert dismissal. The
Gamma CI matrix job runs these tests separately from the color-screen checks.

All previews are **fictional demo readings**, not photos or hardware tests.
The physical OLED remains monochrome with 128×32 discrete pixels; these changes
do not add resolution, alter mining kernels or change frequency/thermal controls.
