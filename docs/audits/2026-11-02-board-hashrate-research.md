# Published S3/S2/C3 and NerdNOS mining rates

Research date: 2026-11-02. These are external projects' published results, not
physical BDLMiner beta.4 measurements. The owner has none of these test boards.
No simulator, cross-compilation or Gamma BM1370 result closes that gap.

## Evidence by target

| Target | Published evidence | What it establishes for BDLMiner |
|---|---|---|
| ESP32-S3 | NMMiner lists 384.5 kH/s for LilyGO T-Display S3 and 400.9 kH/s for Heltec WiFi Kit32 v3 [1, 2]. WarpAxe documents 337.0–339.8 kH/s on a plain T-Display-S3 at 240 MHz, with 36 accepted / zero rejected shares in 32 minutes [10]. | Rates of a few hundred kH/s are plausible on these implementations. These are not measurements of our Cardputer, StickS3, headless or T-Display firmware. No verified 1 MH/s result for these BDLMiner profiles follows. |
| ESP32-C3 | NMMiner advertises a family peak around 402 kH/s; its board table lists XIAO C3 at 396.9 kH/s and a C3 OLED board at 315.1 kH/s [1, 3]. | Useful external reference values, not a BDLMiner speed claim or a universal C3 limit. |
| ESP32-S2 | The S2 has SHA-256 hardware [7]. This search found no sufficiently documented, comparable sustained Bitcoin SHA-256d mining measurement to assign a rate. | Physical beta.4 measurement remains open. Neither clock frequency nor a generic SHA-256 MB/s benchmark gives a valid mining MH/s figure. |
| NerdNOS | OSMU describes a BM1397 ASIC at 150–200 GH/s, approximately 8 W [5]. Another project page lists 80–130 GH/s, 7–8 W [6]. WarpAxe's author reports about 190 GH/s over an hour, pool-counted, on rev 1.3 at 300 MHz, with a case fan and about 73 C; stock firmware was about 180 GH/s at the same clock/pool, with stated ±6–9% uncertainty on each result [4]. | NerdNOS is an ASIC miner; ordinary S3 CPU/peripheral rates do not describe it. Published rates depend on board, firmware and operating conditions. Our 300 MHz default alone does not establish an achieved rate, temperature or stability. |

NMMiner says its hardware results use the highest stable UI refresh and the
Black screen-saver mode [1]. Its pages do not attach a firmware commit, raw
counter series or pool-share validation to each table entry. Treat those as
maintainer-reported figures, not independently reproduced acceptance tests.
The S3 board table does not provide a Cardputer or StickS3 measurement; the
T-Display number must not be assigned to those boards solely because they use S3.
The separate WarpAxe plain-S3 and NerdNOS observations are author reports under
that project's firmware, not a test performed here.

## Checking the StickS3/Cardputer 1 MH/s claim

The owner supplied product pages and two community posts as possible evidence
for 995–1015 kH/s on StickS3 and 950–1050 kH/s on Cardputer. The product pages
confirm StickS3's ESP32-S3-PICO-1-N8R8, 240 MHz dual-core CPU, 135×240 display and
250 mAh battery; they do not provide a mining benchmark [11, 12].

The cited Reddit post explicitly assigns 995–1015 kH/s to **M5StickC Plus2** [13].
Discussion #839 likewise assigns about 1 MH/s to **M5StickC Plus2**, while listing
other supported boards separately [14]. M5Stack specifies ESP32-PICO-V3-02 for
Plus2, distinct from the StickS3's ESP32-S3 [15]. Support for a board does not
establish the same rate on every supported chip.

NMMiner's own README lists up to 1,035 kH/s for ESP32-D0 and up to 398 kH/s for
ESP32-S3; its individual S3 table entries reach about 400 kH/s [16]. None of these
references establishes a 1 MH/s StickS3 or Cardputer result. Equal CPU clocks and
core counts do not establish equal SHA accelerator throughput. Future S3
optimization claims need their own measured-counter and hash-validity evidence.
BDLMiner's own sustained measurements on those boards remain pending.

## A high displayed rate can be wrong

SparkMiner provides a useful counterexample. Its current `v2.9.6-pre` README
explicitly withdraws older S3 figures of 280–400 kH/s: shares were invalid.
It now reports about 50–55 kH/s with software verification, while C3 performance
is still pending real-board validation [8]. The older figures still appear in
another fork's README [9]. They are not retained as valid benchmarks here.
This does not disprove another firmware's SHA implementation; it shows why a
rate display alone is insufficient evidence.

BDLMiner's resumable SHA implementation has reference-digest/self-tests and a
boot-time comparison of direct versus buffered transfers, documented in the
[SHA audit](2026-11-02-resumable-sha.md). Host models establish logic, not silicon
throughput or an absence of hardware interaction faults. Actual sustained work
and accepted shares still need observation on each chip family.

## Optimization lead to measure

WarpAxe's S3 experiment reports about a 16% sustained improvement by preparing
the next SHA block's input while the accelerator processes the current block.
It identifies register-bus latency, especially digest reads, as a major cost;
its DMA and grouped-read alternatives were slower [10]. This is a concrete lead
for a future S3 bench, not evidence of a threefold gain or a rate transferable
to S2/C3. Any equivalent change needs digest checks under contention and actual
hardware timing before being enabled. BDLMiner's existing beta.4 transfer
selection is documented separately; no unmeasured accelerator timing change is
introduced by this research.

## Measurement needed when hardware is available

Record exact model, chip revision, firmware commit, selected SHA backend,
frequency/power settings, display/dashboard load, pool protocol and difficulty.
Compare `total_hashes` deltas against device uptime over equal sustained windows
before/after the change; repeat in reverse order. Check boot identity, health,
full digest correctness and rejected-share reasons. Use a pool/test fixture with
appropriate difficulty, or a long enough observation, to validate submitted work:
at 400 kH/s and difficulty 1, a share averages about three hours. An empty share
count in a ten-minute window does not establish a mining failure or correctness.

Use the [hardware protocol](../v2/hardware-validation.md#other-boards-when-available) for the
counter/health bench. Keep S3/S2/C3 CPU/peripheral tests separate from NerdNOS's
ASIC test. The existing beta.4 binaries need no change to preserve that distinction.

## Sources

1. [NMMiner hardware overview](https://wiki.nmminer.com/docs/hardware/overview/).
2. [NMMiner S3 board table](https://wiki.nmminer.com/docs/hardware/esp32-s3-boards/).
3. [NMMiner C3 board table](https://wiki.nmminer.com/docs/hardware/esp32-c3-boards/).
4. [WarpAxe README, NerdNOS rev 1.3 and ESP32-S3 sections](https://git.warppool.org/dvb-projekt/dvb-WarpAxe). Revision `72b896b18a`, v0.1.39 (2026-10-29), as displayed by the repository during research.
5. [OSMU NerdNOS hardware description](https://osmu.wiki/nerdnos/about/).
6. [Nerd* project overview, NerdNOS section](https://nerdminers.org/#nerdnos).
7. [Espressif ESP32-S2 technical reference manual, SHA accelerator](https://documentation.espressif.com/esp32-s2_technical_reference_manual_en.pdf).
8. [SparkMiner corrected README, commit 16a69a4](https://github.com/SneezeGUI/SparkMiner/blob/16a69a430fd00ff1e8243fc0c68314828b06f52a/README.md).
9. [Older BitzyLabs SparkMiner README, v2.9.1](https://github.com/BitzyLabs/sparkminer/blob/main/README.md), consulted 2026-11-02; not accepted as a validated rate.
10. [WarpAxe S3 peripheral measurements and 30-minute runs](https://git.warppool.org/dvb-projekt/dvb-WarpAxe/src/branch/main/docs/06-Devkit-S3-SHA-Peripheral.md). Document revision `ecbcdca3b5`, dated 2026-10-15; measured C loop `50f6a11`.
11. [M5Stack official StickS3 specifications](https://shop.m5stack.com/products/m5sticks3-esp32s3-mini-iot-dev-kit).
12. [Bastelgarage StickS3 product description](https://www.bastelgarage.ch/m5sticks3-esp32s3-mini-iot-development-kit).
13. [Reddit: M5StickC Plus2 supported in NerdMiner Modded](https://www.reddit.com/r/NerdMiner/comments/1xcdb6a/m5stickc_plus2_now_supported_in_nerdminer_modded/).
14. [NerdMiner discussion #839, BDLMiner announcement](https://github.com/BitMaker-hub/NerdMiner_v2/discussions/839).
15. [M5Stack official StickC Plus2 specifications](https://shop.m5stack.com/products/m5stickc-plus2-esp32-mini-iot-development-kit).
16. [NMMiner README and per-chip/per-board rates](https://github.com/NMminer1024/NMMiner/blob/main/README.md), consulted 2026-11-02.
