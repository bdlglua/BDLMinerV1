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

V2 OTA trial/confirmation transitions, rollback, physical visual acceptance,
Gamma timed stability and longer-duration operation remain unestablished.
NerdNOS and other physical boards remain unvalidated for V2. Earlier V1 hardware
results and loopback software fixtures are not counted as V2 hardware acceptance.
