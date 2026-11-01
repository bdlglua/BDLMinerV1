# BDLMiner V2 implementation record

Work requested: implement the seven V2 features on a dedicated branch, preserving
the published V1 release and user settings. Branch: `v2/development`.

## Deliverables

- [x] Fleet dashboard: optional Linux/Raspberry Pi hub, bounded polling, real
  miner controls, history and explicit offline/stale states.
- [x] One-click pool diagnostics: V1/V2, ASIC version rolling, work availability,
  assigned difficulty and statistical time between shares.
- [x] Assisted Gamma calibration: supervised frequency trials using actual hash
  counter deltas, temperature/error limits, cancellation and settings restoration.
- [x] Understandable statistics: local versus share-based rates, rejected-share
  categories, explicit sample counts and history/current-state separation.
- [x] Custom physical screens: selectable metrics/order/enabled pages, layouts bounded
  to actual resolutions, English/French labels and preserved alert priority.
- [x] Stable/beta updates: signed images, semantic prerelease ordering, explicit
  channel selection, stable defaults and existing bootloader rollback.
- [x] Hardware bench and faster releases: Linux runner for real device checks,
  optional active OTA/reset/fault hooks, truthful skipped/failed reporting,
  parallel build matrix and isolated aggregate signing/publication.

## Design constraints

Per-nonce mining loops, CPU clocks and electrical/thermal/fan interlocks stay intact.
Heavy fleet/history/calibration operations run on the optional host, not on the
ESP32 mining cores. Credentials stay on the host and never enter dashboard JSON,
logs or public reports. New NVS settings have defaults and do not change the
existing lifetime-statistics persistence layout. No V2 beta is published or
installed merely by building this branch.

The cloud workspace has no route to the owner's LAN. Hardware test software can
be exercised against loopback fixtures here, but real hashrate, thermal response,
physical display quality and OTA results require execution on that LAN. No
synthetic result is counted as physical validation.

## Verification

Software verification on 2026-11-01 (UTC):

- All 12 PlatformIO environments built and packaged as unsigned development
  previews. Independent aggregate verification passed for every real binary:
  target/chip/version, ESP checksum/SHA-256, factory application and two OTA slots.
  The largest 4 MB display-board image fills about 89% of its OTA slot.
- Native host suite passed, including SHA-256d equivalence, real Noise/V1/V2
  protocol fixtures, ASIC rolling, shares/block pipeline, thermal/fan policy,
  settings persistence, web/OTA ownership, SemVer and channel cancellation.
- Color rendering checks passed for seven native screen sizes, eight pages and
  English/French. The production Gamma 128×32 renderer passed custom metric order,
  bounded rows, page selection and fault/block alert priority checks.
- Python regressions cover HTTP credential boundaries, offline fleet aggregation,
  calibration supervision/cancellation/restoration/crash recovery, counter resets,
  signed-OTA state transitions, release assembly and stable/beta selection.
- Embedded browser regressions cover the existing dashboard/settings/history and
  new diagnostics, live screen settings, headless behavior and French Gamma UI.
  The actual Python hub also passed Chromium tests using loopback miner fixtures:
  controls, diagnostics, channel state, offline totals, history, translations and
  a 375 px mobile viewport.
- The V2 CI runs those checks and parallel builds, then validates their actual
  aggregate artifacts without signing secrets. Release signing/publication remains
  reserved for an explicitly triggered release workflow.

Physical V2 validation remains pending on M5, Gamma, NerdNOS and the other boards.
The owner's earlier V1 measurements and OTA reports are not counted as V2 tests.
No release/tag was published and no owner's miner was flashed by this work.
Setup and real test commands are in [the V2 guide](README.md).

## Beta readiness follow-up — 2026-11-02 (Europe/Paris)

The next development step combines a first Linux test kit, the embedded dashboard
redesign and mining-work optimization. Portable SHA-256d now specializes its
second hash; NerdNOS no longer recalculates an already verified proof before
submission. The original `prepare()`/`scan()` source and assembly interfaces are
unchanged. Independent hashlib vectors cover empty/null data, every padding
boundary through 512 bytes, larger blocks, unaligned and overlapping buffers.

The optional hub now separates requested/effective power, preserves edits, orders
poll responses and marks lost connections as stale. The CI assembles a validated
M5/Gamma Linux kit with read-only checks and launchers; its scripts never flash a
board automatically. See the [readiness audit](../audits/2026-11-02-v2-beta-readiness.md).
