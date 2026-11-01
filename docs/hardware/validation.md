# Hardware validation from the miner's LAN

The cloud workspace has no private-network/VPN route to the owner's devices;
its read-only attempts received HTTP 403 `Domain forbidden`. That does not
diagnose the miners themselves. The owner has supplied baseline and dashboard
measurements for 1.9.13, confirmed M5/Gamma OTAs to 1.9.14, and a controlled
Gamma rollback from the LAN, recorded below. The owner also confirmed both
physical screens are readable. Controlled thermal/fan response and physical M5
rollback remain pending.
An unavailable NerdNOS board cannot be validated as physical hardware.

`tools/check_hardware.py` uses Python 3's standard library. It **only reads** the
device statistics API and opens dashboards for an operator-confirmed measurement.
It does not upload firmware, alter voltage/power/fan settings or restart a board.
Reports default to `.pio/hardware-report.json` (ignored by Git). Wallets, SSIDs,
settings, logs and web passwords are omitted. If needed, the web password comes
from the environment variable `BDLMINER_WEB_PASSWORD`.

## 4. Actual readings and screen observations

Run from the repository directory on the LAN. Windows uses `py -3` in place of
`python3` below:

```sh
python3 tools/check_hardware.py snapshot http://MINER_IP http://SECOND_IP --output .pio/hardware-snapshot.json
```

Check current firmware/environment, pool connection, measured hashrate,
temperature source and ASIC status. On the Gamma, record the actual fan RPM,
requested/applied duty, ASIC and regulator temperatures, input/core voltage,
current and frequency. Fan-response tests and optical display inspection still
require the physical device; native screenshots alone do not establish these.
The owner reports the earlier electrical interruption was a failed power strip
and that the supply/wiring were checked before active tests.

### Owner-reported baseline, 1 November 2026

The owner supplied a LAN snapshot recorded at 17:41:09 UTC. Both boards were
running **1.9.13**, with pool connections active, no pending firmware trial and
no reported power limitation. These are point-in-time readings reported by the
owner, not observations from this cloud workspace or validation of 1.9.14.

| Board | Reported rate | Temperature | Additional readings |
| --- | --- | --- | --- |
| M5StickC Plus2 | 1.002 MH/s | 53 C, IMU | 0 accepted / 0 rejected; configured thermal limit disabled |
| Gamma 601 | 1.058 TH/s | ASIC 51 C; regulator 57.25 C | 525 MHz; automatic fan 70%, 8,294 RPM; 356 accepted / 1 rejected |

The M5's zero-share count cannot be assessed without the assigned pool
difficulty; the updated tool retains that field. Restoring the M5's default
65 C thermal limit is recommended before the comparison, with the same setting
in both phases. On the Gamma, the requested fan percentage is the saved manual
setting when automatic mode is active. The reported regulator current is on the
core side and must not be interpreted as the 5 V supply current. Optical screen
inspection is covered by the owner's later readability confirmation below;
controlled fan response and physical M5 rollback remain pending.

## 5. Dashboard closed/open measurement

```sh
python3 tools/check_hardware.py measure http://MINER_IP http://SECOND_IP --output .pio/dashboard-measurement.json
```

The tool asks the operator to close existing dashboards, allows 60 seconds for
the rolling rate to settle, then samples both miners for three minutes. It then
opens the dashboards, asks that each remain visible in a separate window, and
repeats with the same sampling cadence. Place the windows side by side; hidden
tabs suspend dashboard updates and would not measure the intended load. Keep
firmware, frequency, power and temperature-limit settings unchanged between
phases. Rates come from actual hash-counter/uptime differences,
not the smoothed display or a guessed pool estimate. Reboots, reset counters,
changed firmware and zero elapsed device time invalidate the comparison. Maximum
observed temperature, share deltas, assigned pool difficulty, effective power
and pool/health conditions accompany each phase. Changed thermal settings,
effective power or interrupted pool connections are flagged for review rather
than silently attributing their effects to dashboard activity.

### Owner-reported dashboard comparison, 1 November 2026

The owner supplied a completed tool-version-2 report at 18:20:29 UTC, with the
closed/open conditions confirmed by the operator. Both boards remained on
**1.9.13** with unchanged boot IDs, effective power at 100% and no reported
limitation or health issue. Each phase retained 18 samples per board over
179–181 seconds. Recomputing rates from the supplied first/last hash and uptime
counters reproduces the report:

| Board | Dashboard closed | Dashboard open | Change | Maximum temperature, closed / open |
| --- | --- | --- | --- | --- |
| M5StickC Plus2 | 1.007335 MH/s | 1.001669 MH/s | -0.563% | 54 / 54 C, IMU |
| Gamma 601 | 1.074122 TH/s | 1.075200 TH/s | +0.100% | 52 / 52 C, ASIC |

Both pools were connected in every sampled response; the Gamma accepted 15
shares in the closed phase and 17 in the open phase, with no new rejects. It
remained at 525 MHz with an automatic fan at 73%, approximately 8,700 RPM and a
regulator temperature of 58–58.25 C in the phase endpoints. The report flagged
no changed comparison conditions. Differences below 1% in these short windows
do not establish a repeatable dashboard penalty; device uptime has one-second
resolution. No large dashboard-related drop or temperature increase was observed
in this comparison. It does not establish long-duration stability or performance
of other boards or the 1.9.14 draft.

The M5 pool assigned difficulty **10,000** over Stratum V2. At about 1 MH/s,
`difficulty * 2^32 / hashrate` gives roughly **497 days per proof meeting the
pool target on average**, before transport/rejection losses; it is not a
deadline or a block-finding estimate. Zero submitted shares over this run are
therefore unsurprising. The client permits an easy target when opening the V2
channel, but the pool chooses the assigned target. The configured V1
suggested-difficulty field cannot force that V2 pool to accept easier shares.
For frequent feedback, use a pool/port supporting ESP32-level difficulty. The M5
thermal limit remained disabled in both phases; the recommendation to restore
its 65 C default still applies.

## 6. Real OTA trial and rollback

Use an appropriate stable supply and the firmware matching the board. Keep the
preceding verified image available. Start observation **before** using the local
dashboard to upload a known, signed CI artifact:

```sh
python3 tools/check_hardware.py ota-monitor http://MINER_IP --previous PREVIOUS_VERSION --expected NEW_VERSION --expect confirm --output .pio/ota-confirm.json
```

A pass requires observing the previous confirmed version, the new version with
a pending trial, then its successful confirmation. Automatic confirmation is
checked after three minutes since board boot, with the pool connected and mining
active (or deliberately paused) at that check. The API's `uptime_s` instead starts
with the mining engine, after initialization; it is not the OTA boot timer.

For a separate controlled bootloader rollback check, start the observer with
`--expect rollback`. The observer prints `Pending trial observed` when it actually
sees the expected firmware in trial. Then perform a physical or serial reset
**before confirmation**, using the board's proper reset control. On a Gamma,
briefly disconnecting and reconnecting its 5 V DC supply also performs this
controlled reset. Do not interrupt the upload or the initial firmware write;
wait for the pending-trial message after the new firmware has booted.
A dashboard restart deliberately confirms the firmware first and is not a
rollback test. The observer must see the previous version return; confirmation,
missing pending-trial evidence or timeouts never count as a rollback pass.
The returned previous version must no longer be in a pending trial. Tool version
3 groups repeated polls with observation counts and first/last UTC timestamps,
keeping the initial observation and at most 199 recent transitions.
This needs physical/serial access and is not performed by the read-only script.

### Owner-reported M5 OTA confirmation, 1 November 2026

At 18:32:08 UTC the owner supplied a completed, non-simulated tool-version-2
report with `observed_result: confirmed`, `saw_pending_trial: true` and
`passed: true`. It records the confirmed 1.9.13 boot, then a different boot
running 1.9.14 in a pending trial, and finally that same 1.9.14 boot with
`firmware_trial: false`. The final sample reports approximately 1.005 MH/s,
54 C (IMU), full effective power, Stratum V2 connected and no pending shares.

The final `uptime_s` is 175, consistent with the mining-session clock starting
after board initialization while the three-minute OTA threshold uses board boot
time. Transient `TimeoutError`/`URLError` entries also occur in the report; it
does not identify their individual causes. The new boot ID remains unchanged
through the observed trial and confirmation. This validates the supplied M5
confirmation sequence, not a physical M5 rollback, prolonged stability
or all settings/statistics surviving an update. The M5 thermal limit is still
disabled; the 65 C recommendation remains unchanged.

### Owner-reported Gamma rollback, 1 November 2026

At 18:41:20 UTC the owner supplied a completed, non-simulated tool-version-3
report with `observed_result: rollback`, `saw_pending_trial: true` and
`passed: true`. The sequence contains a confirmed 1.9.13 boot, a different
1.9.14 boot with a pending trial observed six times from 18:40:56 to 18:41:07,
then a third boot back on confirmed 1.9.13 at 18:41:20 after the requested
controlled physical reset. This is owner-reported evidence of bootloader
rollback on the Gamma; it does not establish a crash-triggered or timeout rollback.

The final sample is from `uptime_s: 0`: the ASIC is stopped and waiting for
pool work, and SV2 is waiting for synchronized time. Hashrate and fan tachometer
are zero, while startup fan duty is forced to 100%. The temperature source is
`board` at 29 C, not the ASIC diode. The saved 525 MHz profile, automatic fan
setting, manual 100% preset and 65 C limit are still present in that sample.
The 5.336 V input is below the firmware's 5.5 V upper limit. The regulator
status is `2112` (`0x0840`), which contains the OFF bit while the ASIC is not
powered and none of the unconditional `kStatusFaults` bits. Low core voltage
and zero RPM at this first stopped sample do not establish a running-state fault.
They also do not prove that mining or fan rotation resumed after rollback.

### Owner-reported Gamma OTA confirmation and mining recovery, 1 November 2026

At 18:56:46 UTC the owner supplied a completed, non-simulated tool-version-3
report with `observed_result: confirmed`, `saw_pending_trial: true` and
`passed: true`. It records the confirmed 1.9.13 boot restored by the rollback,
then a new 1.9.14 boot observed in trial 74 times, followed by confirmation on
that same boot. Two timeout observations occur during the update interval.
The final sample demonstrates resumed mining after installing the candidate:

| Reading | Owner-reported value |
| --- | --- |
| Firmware / trial | 1.9.14 / confirmed |
| ASIC / profile | BM1370 running, 525 MHz; no stock fallback |
| Rolling / live hashrate | 1.066315 / 1.068655 TH/s |
| ASIC / regulator temperature | 54 / 58.75 C |
| Fan | Automatic, 79%, 8,667 RPM; no override |
| Input / core voltage | 5.242188 / 1.144531 V |
| Core-side current | 14.10938 A |
| Regulator status / hardware errors | 0 / 0.079968% |
| Pool / session shares | Stratum V2 connected; 14 accepted, 0 rejected, 0 pending |
| Effective power / configured limit | 100%, no limitation / 65 C |

The saved manual fan preset remains 100% while automatic control applies 79%.
The work credited for the 14 shares matches the assigned difficulty of
2328.306427, allowing for displayed rounding. These readings are within the
firmware's running-state limits; no regulator fault or fallback is reported.
This supplies both Gamma OTA confirmation and running telemetry after the
controlled rollback/reinstallation sequence. It is a short observation, not a
prolonged stability test or controlled manual fan-response test. Physical M5
rollback remains untested; no NerdNOS device was available for this review.

### Owner-reported Gamma V2 corrective build, 1 November 2026

At 20:30:17 UTC the owner supplied a confirmed 1.9.14-to-1.9.15 trial report
for the CI development build correcting the ASIC version-rolling negotiation.
The same new boot is observed pending then confirmed, explicitly connected
over Stratum V2 (Noise). Final telemetry shows the BM1370 running at 525 MHz,
about 1.093 TH/s, ASIC 53 C, regulator 57.5 C, automatic fan 78% at 8,667 RPM,
9 accepted / 0 rejected shares, no pending submissions, regulator status zero
and no stock fallback. This supplies real owner-run V2 recovery evidence after
the earlier 1.9.14 rolling-related stop. The complete
[corrective-build record](../audits/2026-11-01-sv2-asic-version-rolling.md)
documents the artifact checks and remaining validation limits. This development
build has not been published as a signed release at the time of the report.

### Owner-reported screen inspection

After installing 1.9.14 on both boards, the owner explicitly confirmed that both
physical screens are readable in response to a question about legibility,
overlap and clipping. This is an owner observation, separate from the native
renderer tests and from any cloud camera/optical measurement. It covers the
displayed screens at that time, not every long fault/setup message or board.

No synthetic result is substituted for these observations. Retain reports
locally and review them before treating a board/profile as hardware-validated.
