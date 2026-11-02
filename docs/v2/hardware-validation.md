# Four V2 hardware checks

[French operator instructions](hardware-validation.fr.md).

The owner's 2026-11-02 09:48:37 UTC snapshot shows both M5/Gamma on confirmed
`2.0.0-beta.3`: M5 1.005929 MH/s at 48 C IMU; Gamma 1.069368 TH/s at 49 C ASIC,
525 MHz, 70 accepted shares and one stale already present before testing.
These are snapshots, not sustained-rate measurements. The owner separately
confirmed that dashboard gaps and settings icons are fixed on both boards.

| Check | Current evidence / remaining work |
|---|---|
| Local dashboard | Owner accepts gap/icon corrections on both beta.3 devices. |
| Gamma OTA/stability | Beta.3 hour completed: 3,604 s, 1.071274 TH/s, +141 accepted / no new rejected shares, `review` after one recovered timeout. Beta.4 OTA/122-second check passed. New [instrumented hour](#instrumented-gamma-hour) ready for owner execution. |
| Rollback/network recovery | M5 USB reset, rollback, normal beta.4 confirmation, short check and same-boot blacklist recovery passed (1.0052 MH/s over 10 s after recovery). First pool-mode failure retained. |
| S2/S3/C3 and NerdNOS | [External benchmark research](../audits/2026-11-02-board-hashrate-research.md) complete; no test boards available to the owner, so physical BDLMiner measurements remain pending. |

## Gamma hour result

The owner report recorded at 10:55:03 UTC contains 713 successful readings and
the same boot `6a012c1` over 3,604 device seconds. It remains `review`: one HTTP
timeout at +805.063 s recovered, with health unobserved during the gap. ASIC
temperature is 50 C at both endpoints, not an observed whole-hour maximum.
Regulator status `0x0002` is retained as a non-trip warning. The existing stale
share did not increase. The hour has ended; Gamma OTA observation can now proceed.

The command used is retained for reproducibility:

In the current beta.3 kit, in a separate terminal:

```sh
python3 tools/hardware_suite.py --config bdlhub.local.json --miner gamma --seconds 3600 --output gamma-stability-beta3.json
```

Keep Gamma powered, connected and on this version until it finishes. Do not run
Gamma OTA or a shared-network interruption during this hour. M5 work can proceed
independently. A beta.3 result is not a beta.4 stability measurement.

## Instrumented Gamma hour

The old result remains `review`. `tools/trace_hardware.py` runs the existing
counter/health check with the same eight-second request timeout, at most two
additional attempts and unchanged pass/review/fail rules. Close Gamma dashboard
tabs and stop the hub, if running, for an isolated beta.4 measurement. Keep power,
pool, network and settings unchanged; do not perform OTA or network-fault tests
during the hour. Run from the beta.4 kit containing the new host script:

```sh
python3 tools/trace_hardware.py --config bdlhub.local.json --miner gamma --seconds 3600 --dashboard closed --hub stopped --output gamma-beta4-hour-trace.json
cat gamma-beta4-hour-trace.json
```

The script may remain quiet until completion. It changes no device settings.
Dashboard/hub flags only record operator-declared conditions. The normal request
count is unchanged. Timings separate opening/response headers (including any
proxy/DNS/TCP time) from reading the body; they cannot alone identify PC, Wi-Fi or
device-server causes. Up to 32 requests taking at least one second, or raising an
exception, have detailed timing; additional such events are counted.

Only after a recovered transport error, up to three times, two extra timed GETs
capture allowlisted diagnostic numbers and numeric `[loop] stalled` durations.
Raw logs, network exception contents and HTTP headers are not saved. A recovered
stats error still yields `review`/exit 2. An unrecovered error, health fault,
reboot or counter reset fails; a clean window passes/exit 0. A subsequent pass
does not explain the old incident. Inspection shows the HTTP server can wait on
incomplete headers in its synchronous parser; that is a hypothesis, not a proven
cause of this device's timeout. The passed beta.4 short check is not an hour test.

## Extract the beta.4 kit

Download `v2-linux-test-kit` from the successful `v2/development` CI run. Extract
into a new folder; retain the existing kit/settings backup. Check `kit.json`
reports `2.0.0-beta.4` and run `sha256sum -c SHA256SUMS.txt`.
Images are unsigned/manual OTA previews. Only `<board>-firmware.bin` goes through
the local dashboard; `<board>-factory.bin` is for USB installation/recovery.

## M5 rollback first, then normal confirmation

Identify the M5 USB-UART port with `ls -l /dev/serial/by-id/`. That directory can
be absent even when a serial port exists. With a data cable directly between M5
and computer, also check:

```sh
python3 -c 'import glob; print("\n".join(sorted(glob.glob("/dev/ttyUSB*")+glob.glob("/dev/ttyACM*"))) or "No serial port detected")'
lsusb
```

The owner's first listing shows no such port and no identifiable M5 USB bridge.
Check the data cable/computer connection before diagnosing a driver/permission
for an identified USB device. Do not guess `/dev/ttyUSB0` or select unrelated USB
hardware.

A later owner listing shows `/dev/ttyACM0`. Reset preflight now shows boot
`2f867bc` / uptime 6,346 s -> `209bd24` / 38 s, with confirmed beta.3 and reconnected
pool. This verifies the hardware reset, not bootloader rollback.

The owner subsequently observed rollback at 11:15:50 UTC: confirmed beta.3 on
`185f7662` -> pending beta.4 on `da0776f2` -> confirmed beta.3 on `1f4bc50a`, with
`reset_requested: true`. Normal beta.4 reinstallation/confirmation remains pending.

Normal reinstallation subsequently confirmed at 11:22:04 UTC: beta.3 on
`1f4bc50a`, 83 pending beta.4 observations on `ab2a323b`, then confirmation on
that same boot. At uptime 175 s the M5 reports 1.003914 MH/s, 47 C IMU and a
connected pool. Its short counter window and dedicated Internet cut remain pending.

The short check has now passed on 25 readings over 122 s at **1.004855 MH/s**,
with 47 C IMU at both endpoints and no reported read error. Infrequent shares
at difficulty 1 do not make the connected pool incompatible.

The Linux helper
`tools/reset_serial.py --port <M5-port>` uses only DTR/RTS, sends no serial data
or firmware and erases nothing. Verify it on confirmed beta.3 before any upload,
then wait for mining to return. Only select the identified M5 port.
The observer below can then accept
`--reset-command 'python3 tools/reset_serial.py --port <M5-port>'` to request
exactly one reset upon observing the pending beta.4 trial. Substitute the actual
identified port first. Without that option, perform the reset manually.

Before uploading to the M5, start:

```sh
python3 tools/check_hardware.py ota-monitor http://192.168.1.40 --previous 2.0.0-beta.3 --expected 2.0.0-beta.4 --expect rollback --timeout 900 --output rollback-m5-beta4.json
```

Upload `firmware/m5stickc-plus2-firmware.bin` in the M5 Settings page. When the
observer reports the pending trial and says `reset now` (no reset command
configured), perform an identified
physical/serial hardware reset before three-minute automatic confirmation.
Do not use the web restart action: it confirms the trial before restarting.
Do not erase settings. The M5 has a battery, so unplugging USB alone may not reset
it. If no documented hardware-reset method is identified, let it confirm and
record rollback as unperformed; do not improvise long configuration-reset presses.

A pass requires confirmed beta.3, pending beta.4 on a different boot, and confirmed
beta.3 on a third boot. The observer checks all three identities. After rollback,
start a new observer with `--expect confirm --output ota-m5-beta4.json` (same
previous/expected versions), upload the same M5 firmware again and let it confirm.
Then run the 120-second suite with `--miner m5 --output m5-beta4-smoke.json`.

## M5 manual network interruption

Only start with confirmed firmware and active mining. Keep the observer computer
connected. During Gamma's hour, use a per-device Wi-Fi interruption for the M5
only. An Internet-only block may leave the local API reachable and is not a
matching Wi-Fi test. If isolation is unavailable, wait for Gamma's hour to end,
connect the computer by Ethernet, then briefly interrupt the Wi-Fi access point.
Do not power off the miner or save deliberately invalid Wi-Fi credentials.

```sh
python3 tools/monitor_network.py --config bdlhub.local.json --miner m5 --kind wifi --timeout 420 --output network-m5-beta4-blacklist.json
```

Wait for `Observer armed`, interrupt the connection, then restore it as soon as
instructed. Answer yes only if the miner's connection was deliberately interrupted
and restored while the computer remained connected. The observer changes nothing
and cannot restore the router itself. Restore connectivity after any error/Ctrl+C.

It requires two consecutive transport errors, the same boot/identity and ten
seconds of healthy resumed mining with advancing real counters. HTTP gaps alone
cannot identify Wi-Fi loss; missing operator attribution yields `review`/exit 2.
Authentication/TLS/data errors, reboot, counter reset and health faults cannot pass.
`--kind pool` instead requires a reachable API explicitly reporting pool disconnect.

If the router can block only M5 Internet access while retaining its local API:

```sh
python3 tools/monitor_network.py --config bdlhub.local.json --miner m5 --kind pool --timeout 420 --output network-m5-beta4.json
```

V1/V2 detect a silent pool after five minutes. Keep the block until restoration
is requested; the explicit 420-second timeout allows observing that transition.
This verifies pool recovery after an Internet block, not loss of Wi-Fi association.

A router blacklist which also removes local API access requires the
`wifi`/local-connectivity-loss mode, while the observer computer remains connected.
The owner's first `pool` attempt retained 189 successful connected-pool samples,
then three late HTTP timeouts, and is not validated. After removal from the
blacklist the M5 resumes mining on the same boot. The matching retry uses a
separate report filename. **Remove the M5 from the blacklist when restoration
is requested while the observer is still running**, so recovery can be measured.
Also remove the block after an error/timeout.

The new `network-m5-beta4-blacklist.json` now passes: outage, restoration and
operator action confirmed, with boot `ab2a323b` unchanged. Resumed counters
1,553,819,000 -> 1,563,871,000 over uptimes 1,627 -> 1,637 s give **1.0052 MH/s**.
The 60-tick displayed average still includes the intentional pause; live rate
returns to about 1 MH/s. Health during HTTP loss remains unobserved. This is
operator-attributed blacklist/connectivity recovery, while the first failed
pool-mode attempt remains failed.

## Gamma OTA after its hour

The owner has now observed confirmed beta.3 / boot `6a012c1`, pending beta.4 /
boot `93c65b31` (85 observations), then confirmation on that same new boot at
11:02:45 UTC. At uptime 175 s it reports 1.073901 TH/s, 51 C ASIC, six accepted
and no rejected shares. The subsequent check passed on 25 readings over 122 s
on that same boot: 1.073214 TH/s, +3 accepted shares, no new rejection or reported
read error. The stored fault timestamp is unchanged at the 11:09:41 UTC snapshot.
This remains a short beta.4 check.
Persistent `last_fault` records an I2C failure timestamped 10:59:37 UTC, before
the new observed boot. Current mining, regulator status 0 and 1.144531 V core
are reported. The stored fault's cause is unproven; compare its timestamp in
the next snapshot to detect recurrence.

OTA command retained for reproducibility:

```sh
python3 tools/check_hardware.py ota-monitor http://192.168.1.135 --previous 2.0.0-beta.3 --expected 2.0.0-beta.4 --expect confirm --timeout 900 --output ota-gamma-beta4.json
```

Upload only `firmware/bitaxe-gamma-601-firmware.bin` through Gamma's Settings;
retain power/cooling and let it confirm. Then run the 120-second suite with
`--miner gamma --output gamma-beta4-smoke.json`, followed by `sh check-miners.sh`.
`counters_and_health` must pass. Recovered read gaps remain `review`; interrupted
hours fail. Other suite cases marked skipped remain skipped: the separate OTA
and network reports provide their own observations.

## Other boards when available

An actual ordinary S2/S3/C3 and an actual ASIC NerdNOS are needed. Gamma's S3
controls a BM1370; its TH/s rate is not an S3 peripheral benchmark. Use the exact
board firmware. Add the actual board to the host configuration, retaining chip
revision, model, commit, boot log and identical settings/pool/display load.
Beta.4 logs `resumable SHA transfers: direct` or `buffered` and both short kernel
rates; these are not sustained mining rates. Compare real counter deltas over
ten-minute windows before/after updating, repeating with reversed order.
Commit `fa2f61d` predates the transfer optimization. For a configured `s3`:

```sh
python3 tools/hardware_suite.py --config bdlhub.local.json --miner s3 --seconds 600 --output s3-measurement.json
```

No physical gain or 1 MH/s result is established for these families yet.

The [source-backed research](../audits/2026-11-02-board-hashrate-research.md)
records external S3 examples around 0.34–0.40 MH/s, a C3 advertised peak of
0.402 MH/s, no sufficiently documented comparable S2 mining rate, and NerdNOS
BM1397 results in GH/s. These do not validate this firmware's board profiles.
