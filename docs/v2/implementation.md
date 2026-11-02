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

Classic ESP32 assembly mining loops, CPU clocks and electrical/thermal/fan
interlocks stay intact. The later [S2/S3/C3 SHA transfer review](../audits/2026-11-02-resumable-sha.md)
adds a boot-checked kernel with correctness and performance fallbacks; its physical
hashrate remains unmeasured.
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

Owner-operated M5/Gamma V2 readiness and dashboard-load measurements are recorded
in the [beta readiness audit](../audits/2026-11-02-v2-beta-readiness.md#owner-operated-m5gamma-measurements).
The M5 held 1 MH/s; Gamma's open/closed difference was -0.74% in one approximately
five-minute comparison. A subsequent M5 counter/health check passed over 3,604
seconds at 1.004141 MH/s. Gamma's timed check was rejected at its initial health
prerequisite; a later snapshot exposed the bench's overly broad rejection of
nonzero regulator status. Host checks now distinguish firmware shutdown bits
from warnings and retain failure telemetry. These reports do not establish V2
OTA transitions, rollback, physical visual acceptance, Gamma hour-long stability or
longer-duration operation. NerdNOS and the other physical boards remain
unvalidated. Earlier V1 measurements and OTA reports
are not counted as V2 tests. The owner installed the V2 firmware; this workspace
did not flash the boards. No V2 release/tag was published.
Setup and real test commands are in [the V2 guide](README.md).

The owner's subsequent corrected Gamma check passed over 61 device seconds at
1.076066 TH/s, retaining status `0x0002` as a warning and accepting two additional
shares with no rejection. This confirms the host fix on that card; it is not an
hour-long Gamma stability result.

The next owner-operated Gamma run reached 230 successful readings before an HTTP
read failed after about 21 minutes. Its available 1,257-device-second window
measured 1.072458 TH/s with 46 additional accepted shares and no rejection. The
hour-long check remains incomplete; the source of the read failure is unknown.
The collector now classifies read errors and uses bounded retries for transient
statistics GET failures. A completed window containing such gaps is `review`,
not `passed`; reboot/counter/health faults and exhausted retries still fail.

The owner then observed a confirmed beta.1-to-beta.2 manual OTA on the M5:
changed boot identity, pending trial, then confirmation on the same new boot.
The final snapshot reports 1.004112 MH/s, 48 C IMU and connected Stratum V2 at
100% effective power. Gamma beta.2 OTA, V2 rollback, visual checks and sustained
beta.2 rates remain pending; see the readiness audit for the report's scope.

The owner then supplied screenshots of the remaining beta.2 dashboard gap and
settings glyphs. Beta.3 uses a full-width section flow and font-independent SVG
settings/maintenance icons. Software layout checks cover populated sections,
eight viewport widths and retained Gamma fan controls. A later owner snapshot at
09:48:37 UTC shows both devices on confirmed beta.3: M5 1.005929 MH/s / 48 C IMU,
Gamma 1.069368 TH/s / 49 C ASIC, 70 accepted and one stale share already present.
The owner explicitly accepts gap/icon corrections on both local dashboards.
The full beta.3 OTA transition was not observed by the supplied snapshot.

Beta.4 packages the S2/S3/C3 transfer kernel, stricter OTA boot-identity observation
and a read-only manual-network observer. The [ordered hardware protocol](hardware-validation.md)
starts Gamma's hour on beta.3, tests M5 rollback before normal confirmation, then
M5 network recovery and Gamma beta.4 OTA after its hour. Transport loss alone
cannot establish a Wi-Fi cut; operator attribution, the same boot and real
healthy counter progress are required. S2/S3/C3/NerdNOS physical work is explicitly
pending board availability. None of these pending tests is counted as passed.

The next owner Gamma beta.3 hour completed over 3,604 device seconds at
1.071273584906 TH/s, with 713 successful readings, the same boot and +141 accepted
shares / no new rejection. One HTTP timeout recovered at +805.063 seconds, so the
report stays `review` for unobserved health during the gap. It is a completed
mining-counter window, not a clean HTTP pass. Gamma beta.4 OTA remains pending.
The M5 currently has no detected `ttyUSB`/`ttyACM` or identifiable USB bridge in
the owner's listing; cable/connection identification precedes its physical reset.

The subsequent Gamma beta.3-to-beta.4 OTA report observes confirmed old firmware,
85 pending-trial samples on new boot `93c65b31`, then confirmation on that boot.
It ends at 1.073900549 TH/s / 51 C ASIC with six accepted and no rejected shares.
A stored sensor-I2C fault is timestamped before that new boot; its cause remains
unproven and the post-OTA check is pending. A later M5 listing now shows
`/dev/ttyACM0`; its reset preflight is pending before rollback testing.

The M5 USB reset preflight is now observed: `2f867bc` -> `209bd24`, uptime reset
to 38 s, confirmed beta.3 and connected pool. Gamma's beta.4 short counter/health
check passes over 122 device seconds at 1.073213934426 TH/s, with 25 readings,
+3 accepted shares, no new rejection or reported read error, and the same boot.
The prior stored-fault timestamp remains unchanged. M5 rollback/confirmation and
controlled network recovery remain pending their own observations.

The next M5 report validates physical beta.4 rollback: confirmed beta.3 on
`185f7662` -> pending beta.4 on `da0776f2` -> confirmed beta.3 on `1f4bc50a`, with
one configured USB-reset request. Reinstalling/confirming beta.4 and controlled
network recovery remain pending. The returned three-second startup snapshot is
not used as a sustained-rate benchmark.

M5 normal beta.4 reinstallation subsequently confirmed on `ab2a323b` after 83
pending-trial observations, with no reset command. The 175-second snapshot reports
1.003914269 MH/s, 47 C IMU and connected Noise Stratum V2 at difficulty 1. Both
M5/Gamma now have owner-observed beta.4 OTA confirmations. The M5 short counter
window and M5-only Internet block/recovery reports are still pending; ordinary
S2/S3/C3/NerdNOS hardware remains unavailable.

The M5 beta.4 short check subsequently passes over 122 s at 1.004854918033 MH/s,
with 25 readings, the same boot and no reported read error. Its first network
attempt in pool mode fails to observe pool disconnection: 189 successful readings
then three late HTTP timeouts. After the owner removes the router blacklist,
a new snapshot shows restored mining on the same boot. The failed report is
retained. A matching Wi-Fi/local-connectivity observation is pending; the operator
must restore access while the observer runs so recovery can be measured.

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
