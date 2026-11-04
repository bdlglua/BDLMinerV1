# Supported boards

| PlatformIO environment | Boards | Display |
|---|---|---|
| `bitaxe-gamma-601` **experimental** | Bitaxe Gamma **601**, one BM1370, ESP32-S3 N16R8 | SSD1306 OLED 128×32 |
| `nerdnos` **experimental** | [NerdNOS](../hardware/nerdnos.md): one BM1397 on a board carrying a T‑Display‑S3 | ST7789 170×320 (parallel) |
| `m5stickc-plus2` (default) | M5StickC Plus2 | ST7789V2 135×240 |
| `m5stack-esp32` | StickC / StickC Plus, Core Basic/Gray/Fire, Core2, Tough, Atom Lite/Matrix, Stamp Pico… (auto-detected) | depends on model |
| `m5stack-s3` | AtomS3 / S3R / S3 Lite, CoreS3 / SE, Cardputer / ADV, StampS3, Dial, DinMeter, StickS3… | depends on model |
| `lilygo-t-display` | LilyGO TTGO T‑Display (ESP32) | ST7789 135×240 |
| `lilygo-t-display-s3` | LilyGO T‑Display‑S3 | ST7789 170×320 (parallel) |
| `esp32-2432s028r` | ESP32‑2432S028R "Cheap Yellow Display" | ILI9341 320×240 |
| `esp32-headless` | any ESP32 board (LED on GPIO2) | — (serial + web) |
| `esp32s3-headless` | any ESP32‑S3 board | — (serial + web) |
| `esp32s2-headless` | any ESP32‑S2 board | — (serial + web) |
| `esp32c3-headless` | any ESP32‑C3 board | — (serial + web) |

The [board SHA review](../audits/2026-11-02-resumable-sha.md) distinguishes chip
families, the classic ESP32 revision requirement and pending S2/S3/C3 measurements.
Gamma 601 has owner-operated V2 measurements around 1.07 TH/s; NerdNOS has no
physical validation recorded here. Both use an ASIC. Read their guides first:
[Gamma 601](../hardware/bitaxe-gamma-601.md) (replaces AxeOS; 5 V ≥4 A barrel supply
and active cooling) and [NerdNOS](../hardware/nerdnos.md).

[Back to the project overview](../../README.md#supported-boards).
