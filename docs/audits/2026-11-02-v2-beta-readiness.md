# V2 beta readiness — 2 November 2026 (Europe/Paris)

Scope: prepare a first testable beta, redesign the miner's own local dashboard
and optimize mining work on `v2/development`. The published stable release remains
1.9.15. Owner-operated M5/Gamma measurements below cover a limited part of V2
hardware acceptance.

## Mining changes

Generic double SHA-256 formerly serialized its first digest, invoked the general
SHA padding path again and reloaded those digest bytes. The second digest now
uses the eight existing SHA words, fixed 256-bit padding, an unrolled schedule and
constant initial state. Its schedule is kept out of the first hash's padding stack.

NerdNOS formerly hashed a returned header in its driver and then hashed it again
in the common submission adapter. The shared adapter now returns the independently
verified difficulty and enforces the existing hardware threshold before recording
a result. The driver reuses that result for error accounting and credited work.
Duplicates, stale generations, unnegotiated bits and below-filter results retain
their rejection paths. Gamma uses the same adapter with its existing threshold.

The source from `sha::prepare()` through `sha::scan()` is byte-for-byte unchanged
relative to 3eb9b5a. Hardware/software assembly scan loops, voltage, frequencies,
thermal/fan policy and hashrate-counter accounting are unchanged.

Independent `hashlib` checks cover 517 input lengths, including null/empty input,
unaligned memory, in-place output, SHA padding boundaries and multi-block data.
The native SHA scan, ASIC adapter, NerdNOS, block pipeline and real V1/V2 protocol
fixtures are exercised by `./test/host/run.sh`.

## Benchmark scope

[Raw native measurements](../benchmarks/v2-native-sha256d-2026-11-02.json) record
the old commit, both SHA source hashes, compiler/flags, architecture, seven
alternating samples and median rates for 64/80-byte inputs. One warmup round is
excluded. Reproduce with:

```bash
python3 tools/benchmark_sha256d.py --baseline 3eb9b5a --iterations 750000 --runs 7
```

These are portable SHA-256d measurements on the host computer. They do not imply
an equivalent increase in M5 hardware SHA MH/s or ASIC TH/s, and they do not
measure cooling or electrical efficiency. Board-level counter measurements are
still required to establish any actual throughput/temperature benefit.

## Dashboard and test kit

The embedded page prioritizes current work and clearly separates a running ASIC,
cooling/pause and pool connection. Diagnostics expand on demand. Graph controls,
export, accessibility and EN/FR labels retain their regression coverage.

The host hub uses the configured power limit in its control and displays effective
power separately. Poll ordering prevents old reads/errors from replacing a newer
applied setting. Browser tests cover unsaved power edits, delayed chart/fleet
responses and lost/recovered connections.

The offline Linux kit validates its M5/Gamma firmware hashes, chip, target/version,
factory application and both OTA capacities. It contains public verification
material, explicit scripts/tools, checksums and EN/FR instructions. No private keys,
runtime database or local reports are copied. Archive creation checks the manifest
again and excludes newly created caches. Input corruption does not replace a
previous valid kit.

`check-miners.sh` only reads statistics. A V1 board, pending firmware trial,
stopped/paused miner, disconnected pool or failed health check cannot receive a
V2-ready result. No script automatically installs a firmware or changes frequency.
The CI's `v2-linux-test-kit` artifact contains unsigned development images.

## Owner-operated M5/Gamma measurements

The owner supplied real `fleet-readiness` and `check_hardware.py measure` JSON
reports on 2 November 2026. The measurement report was recorded at 02:43:33
Europe/Paris (01:43:33 UTC). Both devices report `2.0.0-beta.1`, confirmed firmware,
and the same boot identity throughout the supplied observations. These results
were collected on the owner's LAN; the cloud workspace did not contact the
boards. A firmware version alone does not attest the exact installed source commit.

The comparison used one ordered pair of approximately five-minute phases,
dashboards closed then open, following 60-second warmups. The operator was
instructed to keep the hub active and settings unchanged. Rates below were
recalculated from the reported total-hash and device-uptime counter deltas.

| Device | Closed | Open | Change | Maximum reported temperature, both phases |
| --- | ---: | ---: | ---: | --- |
| M5StickC Plus2 | 1.004193 MH/s (295 s) | 1.005083 MH/s (300 s) | +0.0886% | 51 C (IMU) |
| Bitaxe Gamma 601 | 1.073670 TH/s (297 s) | 1.065683 TH/s (300 s) | -0.7439% | 52 C (ASIC) |

Both devices reported 100% effective power and no limiting reason other than
`full`. Their pools were connected at all 28 closed-phase and 29 open-phase
readings. Reported health-issue lists and rejected-share deltas were empty/zero.
Gamma boundary snapshots retained 525 MHz, automatic fan control, no stock
fallback and zero regulator fault status.

The M5 already had one accepted ordinary pool share at difficulty 1 before the
comparison and retained it with no rejection; no new share arrived during these
short phases. Gamma accepted 9 and 13 shares in the respective measurement
windows, with no pool rejection, reaching 71 accepted in total. Pool rejection
counts do not imply an absence of ASIC hardware errors.

The M5 held its 1 MH/s target with the dashboard open in this test. Gamma's
sub-1% difference in one short ordered comparison does not establish persistent
dashboard overhead or isolate it from throughput variation. Intermediate samples
were summarized by the owner's tool rather than supplied individually, and the
hub process state was not independently observed. This report does not prove
long-term memory stability or continuous connectivity between samples. The M5's
temperature is from its IMU, not the CPU junction; its configured thermal-limit
value remained 0 in both phases.

### M5 hour-long check and Gamma bench-status correction

The owner subsequently supplied `m5-stability.json`, recorded at 04:09:02
Europe/Paris (03:09:02 UTC). Its counter/health case passed over 3,604 device
seconds, with a recalculated rate of 1.004141 MH/s and an unchanged boot identity.
Accepted shares increased from 1 to 4, with no rejected shares. Boundary readings
were 51 C IMU; free heap changed from 58,216 to 56,788 bytes with a 40,948-byte
largest block at both boundaries. These two readings do not establish a memory
trend or a temperature maximum. Pool diagnostics passed; OTA, network-fault and
physical-display cases were explicitly skipped.

`gamma-stability.json`, recorded at 08:31:52 Europe/Paris (07:31:52 UTC), failed
the initial health prerequisite before starting its timed counter measurement.
Its subsequent pool diagnostic passed, but that does not validate the missing
hour-long test. The old bench discarded the initial rejected reading, so its
exact state cannot be recovered from this report.

The owner supplied a fresh Gamma snapshot at 08:35:17 Europe/Paris (07:35:17 UTC):
1.064061 TH/s, 829 accepted / 0 rejected shares, 525 MHz, 51 C ASIC, 57 C regulator,
5.25 V input, 1.144531 V core and 7,114 RPM automatic fan. It retained the earlier
boot identity at 23,313 seconds uptime and reported `regulator_status: 2` (CML).
The old host bench rejected every nonzero regulator status, whereas the firmware
stops for OFF, VOUT_OV, IOUT_OC, VIN_UV or TEMP (`0x007c` while powered). The new
snapshot reproduces a host-only false rejection; it does not prove the discarded
initial reading had precisely the same status.

The host bench/readiness health check now uses those firmware shutdown bits,
rejects missing/invalid status and retains other status bits as warnings in the
counter report. Failure reports retain the first/last available readings,
elapsed time, sample count, failure stage and individual health reasons. A
transport error keeps the last successful reading without declaring it unhealthy.
The existing calibration supervisor retains its stricter preconditions for
active frequency trials. Firmware interlocks and electrical settings are unchanged.
Python regressions cover each STATUS_WORD bit, CML warning retention, real fault
bits, initial rejection, read timeout, reboot evidence and successful counter rates.

The owner then supplied `gamma-check.json`, recorded at 08:44:05 Europe/Paris
(07:44:05 UTC), using the corrected host script with a requested 60-second run.
Counter/health and pool-diagnostic cases both passed: 12 readings, 61 device
seconds, and a recalculated 1.076066 TH/s. The same boot identity was retained;
shares increased from 845 to 847 with zero rejected shares. Boundary readings
showed 525 MHz, 100% effective power, 50 C ASIC and 56.5/56.25 C regulator. Status
`0x0002` remained present at both boundaries and was retained as
`regulator_warning_bits: 2` with a warning. This validates the host correction on
the owner's Gamma without clearing its status or changing firmware protections.
It is a short check, not the still-pending hour-long Gamma measurement. Raw
intermediate readings and physical/OTA/network-fault tests were not supplied.

The owner next attempted the 3,600-second Gamma run. Its report, recorded at
09:08:30 Europe/Paris (08:08:30 UTC), failed after 1,271.254 host seconds on a
statistics read (`DeviceError`, `failure_stage: sample_read`, 230 successful
readings). The available counter window covers 1,257 device seconds at a
recalculated 1.072458 TH/s. Boot identity and firmware stayed unchanged at the
boundaries, shares increased from 850 to 896 with no rejection, and boundary
ASIC readings were 50 C at 525 MHz / 100% power. The regulator warning remained
`0x0002`. The pool diagnostic responded after the failed read, but the report
cannot distinguish a host/network issue from delayed firmware HTTP service or
establish mining/health during the missing read. This remains a failed,
incomplete hour-long test; it is not retrospectively converted to a pass.

The host client now classifies redacted timeout, connection, HTTP and TLS errors.
Only statistics GET reads may receive up to two additional attempts, two seconds
apart. An observed health fault, reboot, counter reset, authentication error or
malformed response still fails. A full counter window with recovered read errors
is `review` (exit 2), never a clean pass; exhausted retries still fail. Reports
retain error counts, up to 32 error events, recovery counts and the available
partial counter window after a read failure. Adjacent counter checks also catch
a decrease that remains above the initial baseline. These collection changes do
not identify or fix the unknown cause of the owner's interrupted HTTP read.

The owner then supplied the Gamma ring log. Its time prefixes are **device
uptime**, as formatted by `src/log.cpp`, not local/UTC wall-clock times. The last
counter reading (25,291 seconds) corresponds to uptime 07:01:31. Subsequent
30-second mining log entries continued, with accepted shares advancing from
896 to 921 and zero pool rejections through uptime 07:15:09. This supports
continued mining after the bench's failed read, without proving web availability
or health at every instant in the missing interval.

The same log repeatedly reports `[share] hashrate not shared: HTTP 400`. This
is the anonymous community statistics upload, not a pool share rejection.
The separate BDLMiner-stats service's parser at `126ec1c` only accepts numeric
stable versions and rejects `2.0.0-beta.1`. A bounded SemVer compatibility fix
passed all 19 service tests and is provided as a
[patch](../patches/BDLMiner-stats-v2-semver.patch) with
[application instructions](../patches/README.md). Pushing that separate repository
returned HTTP 403; the owner then applied and pushed the fix from their own PC
as `009fa2da830d6a5a80e56263f2dd44ad2bf5831c`. The
[service workflow](https://github.com/bdlglua/BDLMiner-stats/actions/runs/41414353158)
passed all 19 tests and its actual (non-skipped) Cloudflare deployment step
succeeded at 09:37 Europe/Paris (08:37 UTC). Deployment logs identify Worker
version `d8c4fff5-57ad-46c6-a009-35b27e34eea4`. The fix is now deployed; acceptance
of the next real miner report has not yet been observed here. This HTTP 400 does
not establish the cause of the earlier local statistics-read interruption.

### Beta.2 M5 manual OTA confirmation

The owner supplied `ota-m5-beta2.json`, recorded at 09:54:44 Europe/Paris
(08:54:44 UTC). It observes a confirmed beta.1 baseline, a changed boot identity
running beta.2 on trial, then `firmware_trial: false` on that same new boot.
The monitor reports `confirmed`, `saw_pending_trial: true` and `passed: true`.
One timed-out read occurred during the upload/reboot transition; subsequent
observations resumed and captured the trial and its confirmation.

The final snapshot at 175 seconds mining uptime reports 1.004112 MH/s (1.001015
MH/s live), 48 C IMU, 100% effective power, a connected Stratum V2 pool and
70,708 bytes free heap / 55,284-byte largest block. New-session shares are zero
at difficulty 1; this short snapshot is not a share-acceptance or sustained-rate
test. Firmware version alone does not attest the exact installed source commit.
This establishes an owner-operated M5 beta.1-to-beta.2 OTA trial/confirmation,
not rollback or physical/local-dashboard visual acceptance.

The owner subsequently reported a remaining display problem and supplied two
screenshots: font-symbol settings navigation and a gap beneath pool diagnostics.
Beta.2 still kept a narrow Bitcoin column beside the chart. Beta.3 removes that
two-column dependency and lays chart, diagnostics, pool and Bitcoin sections in
one full-width flow. Settings/maintenance icons are inline SVGs with text labels.
Loopback rendering with populated pool, block and share fixtures reproduced a
248.7 px left-column tail in beta.2; the revised flow has only the ordinary 20 px
gap between occupied panels. Bounds were checked at eight widths (320-1667 px),
with SVG navigation and retained Gamma fan controls. These are software layout
checks; beta.3 has not yet been observed on the owner's cards.

### Confirmed beta.3 snapshot and owner visual acceptance

At 2026-11-02 09:48:37 UTC, the owner supplied a `snapshot` report with both devices
reachable and `firmware_trial: false` on `2.0.0-beta.3`:

| Device | Boot / uptime | Reported rate | Temperature | Shares accepted/rejected |
|---|---|---:|---|---:|
| M5StickC Plus2 | `2f867bc` / 1,683 s | 1.005929 MH/s | 48 C IMU | 0 / 0 |
| Gamma 601 | `6a012c1` / 1,684 s | 1.069368 TH/s | 49 C ASIC | 70 / 1 stale |

Both show 100% effective power and connected Stratum V2 (Noise). Gamma remains
at 525 MHz, with 7,427 RPM automatic fan at 65%, 55.5 C regulator, status 0,
5.25 V input and 1.144531 V core. Reported hardware errors are 0.041894%.
The stale share is already present before the next tests; its cause and any
rejection-rate trend cannot be inferred from this snapshot.

The owner separately answered yes when asked whether the large gaps were gone
and settings icons correct on both local dashboards. This is owner-operated
visual acceptance of those corrections, not a physical-screen or full UI audit.
The snapshot establishes confirmed beta.3 running on both cards, not an observed
complete OTA transition, sustained rate or hour-long stability.

Beta.4 is a distinct unsigned development build for the
[four-check hardware protocol](../v2/hardware-validation.md), with the S2/S3/C3
transfer kernel and improved host observers. Beta.4 OTA, V2 rollback, actual
network interruption/recovery, Gamma hour-long stability and longer-duration V2
operation remain pending physical reports. NerdNOS and ordinary S2/S3/C3 boards
are unavailable to the owner and remain unvalidated. Earlier V1 results and
loopback software fixtures are not counted as V2 hardware acceptance.

### Gamma beta.3 hour completed with a recovered HTTP gap

The owner supplied `gamma-stability-beta3.json`, recorded at
2026-11-02 10:55:03.031971 UTC. Its overall and counter/health status is `review`.
The requested 3,600-second window completed with 713 successful readings and
3,604 device seconds (3,604.724 host seconds). All retained readings passed the
bench's health and adjacent-counter checks; this does not establish health or
HTTP availability during the missing read.

The first/last boot is `6a012c1` on confirmed beta.3; uptime advances
2,064 -> 5,668 s and hashes 2,202,962,000,000,000 -> 6,063,832,000,000,000.
Recalculated throughput is **1.071273584906 TH/s**. Accepted shares advance
85 -> 226 (**+141**) and rejected shares remain 1 (the pre-existing stale).
Both endpoints show 525 MHz, 100% effective/configured power, connected Noise
Stratum V2, 50 C ASIC, 5.242188 V input and 1.144531 V core. Regulator temperatures
are 57 -> 56.25 C; automatic fan readings are 8,047 -> 7,267 RPM. These endpoint
values are not full time-series extrema. Free heap changes 158,052 -> 153,460
bytes with the same 131,060-byte largest block; endpoints alone do not establish
a memory trend.

One statistics read timed out at +805.063 host seconds, attempt 1, then recovered.
`read_error_count` and `read_recoveries` are both 1. The report correctly retains
`review` because health during the gap is unobserved; no timeout cause is proven.
Regulator status 0 -> 2 is retained as `regulator_warning_bits: 2`, not a shutdown
fault or the reason for review. Pool diagnostics pass (`ready`, 16 rolling bits
for eight required). Controlled network recovery, OTA and physical-display cases
remain explicitly skipped. This establishes a completed beta.3 mining-counter
window with one recovered HTTP gap, not a clean HTTP stability pass or a beta.4
result. Gamma beta.4 OTA can now be observed without interrupting the hour.

The owner's M5 USB diagnostic shows no `/dev/serial/by-id/`, no `ttyUSB`/`ttyACM`
port and no identifiable M5 USB bridge in the supplied `lsusb` listing. Cable/
connection identification is pending; no hardware reset or rollback was performed.

### Gamma beta.4 OTA confirmation and persisted prior fault

The owner then supplied `ota-gamma-beta4.json`, recorded at 11:02:45.774675 UTC
with tool version 4. The observer reports `confirmed`, `saw_pending_trial: true`
and `passed: true`: confirmed beta.3 on `6a012c1` (15 observations), pending
beta.4 on new boot `93c65b31` (85 observations from 10:59:51.530183 through
11:02:43.737137 UTC), then confirmed beta.4 on that same new boot at
11:02:45.774633 UTC. No reset command was requested. This establishes an
owner-observed Gamma beta.3-to-beta.4 OTA trial and confirmation.

The final snapshot at uptime 175 s reports 1.073900549 TH/s, 51 C ASIC, 525 MHz,
100% effective power, six accepted shares and no rejection over Noise Stratum V2.
Fan is automatic at 68% / 8,047 RPM; regulator is 56.5 C, status 0, core voltage
1.144531 V and input 5.242188 V. These are current endpoint readings, not sustained
beta.4 acceptance. The requested post-OTA counter/health check is still pending.

The snapshot also includes a persisted `last_fault`: sensor I2C failure, regulator
status `0x0842`, core 0.025 V and unavailable diode reading. Its Unix timestamp
1793617177 is **10:59:37 UTC**, before the new observed beta.4 boot and after the
completed beta.3 hour. `gamma601::saveFault/loadFault` deliberately preserves it
across boots. Current running/voltage/status readings do not report that stopped
state. The fault's cause, including any relationship to OTA, is unproven; retain
the record and compare its timestamp after the post-OTA check. No safety check
or fault-latching behavior was relaxed.

A subsequent owner USB listing now shows `/dev/ttyACM0` after reconnecting the
M5. Hardware-reset preflight has been requested on confirmed firmware, before
starting its rollback OTA. Detection alone does not validate the reset.

### M5 physical USB reset and Gamma beta.4 short check

The M5 preflight report shows confirmed beta.3 on boot `2f867bc`, uptime 6,346 s
at 11:06:22.261743 UTC. The owner ran `reset_serial.py --port /dev/ttyACM0`, which
reported a reset pulse. At 11:07:08.103472 UTC the same M5 reports new boot
`209bd24`, uptime 38 s, confirmed beta.3, connected Noise Stratum V2, 100% power
and 49 C IMU. Early post-boot live rate is 0.994917 MH/s; this is not a sustained
performance window. This validates the chosen USB hardware-reset path without
flashing or erasing settings. It does not yet validate bootloader rollback.

Gamma's subsequent beta.4 short check, recorded at 11:09:41.672401 UTC, passes
both counters/health and pool diagnostics on 25 successful readings. Boot stays
`93c65b31`; uptime 469 -> 591 s and hashes 488,281,900,000,000 ->
619,214,000,000,000 give **1.073213934426 TH/s over 122 device seconds**.
Accepted shares advance 18 -> 21, with no rejection or reported read error.
Endpoints show 50 C ASIC, 525 MHz, 100% power, regulator status 0, 1.144531 V core
and 5.242188 V input. The follow-up snapshot at 11:09:41.789078 UTC retains the
same prior fault timestamp, 1793617177, and has no reported health issue. This
is a clean short post-OTA check, not an hour-long beta.4 result or a proof of the
stored I2C fault's cause. Suite network/OTA/display skips remain skips; the
separate OTA report supplies its own confirmation evidence.

### M5 beta.4 physical bootloader rollback

The owner supplied `rollback-m5-beta4.json`, recorded at 11:15:50.713968 UTC,
tool version 4, `observed_result: rollback`, `saw_pending_trial: true`,
`reset_requested: true` and `passed: true`. The observer begins with confirmed
beta.3 on `185f7662`, observes pending beta.4 on `da0776f2` at 11:15:40.225745 UTC,
requests the configured USB reset once, then sees confirmed beta.3 on third boot
`1f4bc50a` at 11:15:50.713926 UTC. Temporary timeout/URL errors are retained around
the OTA/reboot transitions. They are not a controlled network-loss test.

The returned beta.3 snapshot is only three seconds into startup, with pool
connected and 53,248 hashes. Its 13.867 kH/s startup reading is not a sustained
mining-performance measurement. The report establishes physical M5 bootloader
rollback. Normal beta.4 reinstall/confirmation and the planned M5-only Internet
block/recovery test still require their own observations. The pool-mode observer
will use a 420-second timeout because firmware detects a silent V1/V2 pool after
five minutes; no pool timeout or fault policy is changed for the test.

### M5 beta.4 reinstallation and normal confirmation

The next owner `ota-m5-beta4.json`, recorded at 11:22:04.275038 UTC, reports
`confirmed`, observed pending trial, no reset request and `passed: true`.
It begins on returned beta.3 boot `1f4bc50a`, observes 83 pending beta.4 samples
on `ab2a323b` from 11:19:09.837566 to 11:22:02.199559 UTC, then confirmation on
that same boot at 11:22:04.274996 UTC. A timeout during the planned upload is
retained; no restart during the observed pending trial is reported.

At uptime 175 s the M5 reports 1.003914269 MH/s (live 0.999175758 MH/s), 47 C IMU,
100% power, confirmed firmware and connected Noise Stratum V2 at difficulty 1.
Shares are zero in this short new session; this is not a share-acceptance test.
This establishes owner-observed normal beta.4 OTA confirmation after the physical
rollback test. The requested M5 short counter/health check and M5-only Internet
block/recovery still need their own reports. Both boards currently report
confirmed beta.4; ordinary S2/S3/C3/NerdNOS hardware remains unavailable.

### M5 beta.4 short check and first network attempt

The owner supplied `m5-beta4-smoke.json`, recorded at 11:26:52.091554 UTC.
Counters/health and pool diagnostics pass on 25 readings: boot `ab2a323b`, uptime
341 -> 463 s and hashes 338,493,400 -> 461,085,700 yield
**1.004854918033 MH/s over 122 device seconds**. Both endpoints show 47 C IMU,
100% power, confirmed beta.4, connected Noise Stratum V2 and no shares/rejections.
No read error is reported. Diagnostics are `infrequent_shares` but compatible:
difficulty 1 implies a mean share interval around 4,275 seconds, so zero shares
in this short check do not establish a failure.

The first manual network report starts at 11:27:09.335646 UTC in `pool` mode and
fails after 423.66 host seconds with `No matching outage observed before timeout`.
Its 189 successful readings retain boot `ab2a323b`; first/last uptime is 481 ->
874 s and both retained pool states are connected. Three HTTP timeouts occur at
+403.643, +413.652 and +423.66 seconds. No reachable disconnected-pool state was
observed, so the restoration and operator-confirmation phases were not reached.
Their false fields do not prove that the operator performed no action, nor
independently describe the miner's later state.

The owner initially had not restored access after timeout; restoration was
requested. A snapshot at 11:37:45.824567 UTC then shows the same boot at uptime
1,117 s, pool connected, 1.004638159 MH/s reported, 46 C IMU and no health issue.
The owner identifies the action as adding/removing the M5 in the router blacklist.
The observed local HTTP loss did not match the requested pool-only test. The
first report remains failed; no network recovery pass is retroactively assigned.
A matching `wifi`/local-connectivity observation with a distinct filename and
explicit restoration during the running observer has been requested. No firmware
timeouts, health interlocks or pass criteria were weakened.

### Matched M5 blacklist/connectivity recovery passes

The owner supplied `network-m5-beta4-blacklist.json`, starting at
11:43:16.064509 UTC in `wifi` mode. It reports `passed`, observed outage,
restored connectivity and `operator_confirmed: true`, with 75 successful
readings over 199.827 host seconds. Boot stays `ab2a323b` on confirmed beta.4.
Three planned-interruption HTTP timeouts are retained at +151.261 and +161.269 s
during outage detection, then +171.277 s during recovery. The operator confirms
adding/removing only the M5 in the router blacklist while the computer stays
connected. This attribution is part of the test; HTTP loss alone is not
independent proof of Wi-Fi radio state. Health during the HTTP outage remains
explicitly unobserved.

After restoration, the observer sees connected Noise Stratum V2 and counts
1,553,819,000 hashes at uptime 1,627 s, then 1,563,871,000 at 1,637 s:
**1.0052 MH/s over ten resumed device seconds**, matching the report. The final
snapshot has 45 C IMU, 100% power and live rate 1.000359041 MH/s. Its displayed
60-tick average is 0.366686646 MH/s because the window still includes the
intentional pause; counters and live rate independently establish resumed work.
The first failed pool-mode attempt remains failed. The matching report validates
same-boot local-connectivity/pool recovery after the operator's blacklist action,
not uninterrupted mining or long-duration beta.4 stability.

Current scope: owner accepts the local layout fixes; both beta.4 OTA confirmations,
M5 physical rollback, both short counter windows and matched M5 network recovery
are observed. Gamma's beta.3 hour remains `review` after one recovered HTTP timeout.
Ordinary S2/S3/C3 and NerdNOS hardware are still unavailable, so no physical gain
or 1 MH/s claim for those chip families is added.

### External board research and instrumented Gamma rerun prepared

The owner requests published S3/S2/C3/NerdNOS research and resolution of the
Gamma hour's recovered HTTP timeout. The [research record](2026-11-02-board-hashrate-research.md)
separates maintainer claims, a documented external S3 run, an external NerdNOS
pool-counted hour and missing S2 evidence. It also records SparkMiner's withdrawal
of invalid high S3 figures. No external rate is assigned to BDLMiner's profiles;
ordinary board hardware is still unavailable.

`tools/trace_hardware.py` adds host-side HTTP phase timings to the existing
counter/health bench. It retains the standard eight-second timeout, retry limit
and `review` after any recovered stats error. Normal sampling adds no requests;
after a recovered timeout/connection error it makes two bounded diagnostic GETs,
at most three times, retaining only allowlisted numbers and loop-stall durations.
Tests distinguish header/body timeouts, preserve the original review result and
check that raw logs, URLs, credentials and network exception text are not emitted.
Kit tests exercise the distributed CLI imports after extraction.

The server selects a waiting client once bytes are available, before establishing
that its complete request headers have arrived. The Arduino synchronous header
parser can then block while waiting for more bytes. This is a code-inspection
hypothesis only; no evidence ties it to the physical +805.063 s timeout. No
firmware parser, timeout, health interlock or rate counter is changed. A new
owner-operated beta.4 hour with dashboard closed and hub stopped remains to run;
the old beta.3 `review` and all other original reports retain their status.

All 83 host-tool tests pass locally, including seven tracing regressions and the
extracted-kit CLI/import check. Adding only this new script to the unchanged
local beta.4 kit also passes `--help`. Those checks are software evidence; no
new physical Gamma hour or other-board measurement is claimed.

### Gamma beta.4 instrumented hour passes

The owner supplied the complete displayed JSON from `gamma-beta4-hour-trace.json`,
recorded at **2026-11-02 13:23:53.091316 UTC**. It reports `simulated: false`,
`read_only: true`, tool version 1 and `passed` for the counter/health window on
confirmed `2.0.0-beta.4`, Gamma 601 / BM1370. This is owner-operated physical
evidence transcribed from the supplied report, not an independent LAN read by
this workspace. The tracing tool is from commit `a31fef8`; its
[CI run](https://github.com/bdlglua/BDLMinerV1/actions/runs/41444120904) passed.

All 715 successful samples keep boot `93c65b31`. Uptime 5,038 -> 8,642 s gives
3,604 device seconds (3,604.475 host seconds). Counter difference
9,244,286,000,000,000 - 5,386,598,000,000,000 = 3,857,688,000,000,000 hashes yields
**1.070390677026 TH/s**, matching the report. Accepted shares rise 182 -> 301
(+119); rejected shares stay at one, the existing stale. Both endpoints have
connected Noise Stratum V2, 525 MHz, 100% power, 51 C ASIC and 57 C regulator.
These endpoint temperatures do not establish whole-hour maxima. The bench
reports no health fault; the accumulated non-trip regulator warning `0x0002`
is retained, with endpoint status 0 -> 2.

HTTP timing records 715 requests, all stats reads, **zero request exceptions**,
47.898 ms mean and 987.599 ms maximum. No request reaches the one-second slow
threshold; detailed slow/failed events and post-recovery contexts are empty.
There is no recovered-read error in the bench, so this is a clean hour under
the original eight-second timeout, retry and health criteria.

Operator-declared conditions are dashboard closed and hub stopped. This closes
the pending beta.4 isolated hour; the beta.3 hour remains `review` in its own
record. No claim is made that a firmware fix eliminated its timeout, that the
parser hypothesis caused it, or that dashboard/hub load has been validated for
an hour. No firmware/settings change or further physical run is needed to record
this result. Current four-check scope: local layout accepted, Gamma OTA and
isolated hour passed, M5 rollback/OTA/short check/blacklist recovery passed;
external board research complete, own S2/S3/C3/NerdNOS measurements still pending
hardware availability.
