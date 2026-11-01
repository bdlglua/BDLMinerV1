# Hardware validation from the miner's LAN

The cloud workspace has no private-network/VPN route to the owner's devices;
its read-only attempts received HTTP 403 `Domain forbidden`. That does not
diagnose the miners themselves. Physical display appearance, real thermal/fan
response, dashboard-load performance and bootloader rollback remain unmeasured
until these checks run on the LAN or a reviewed private-network route is provided.
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
inspection, fan response, dashboard load and real OTA/rollback remain pending.

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

## 6. Real OTA trial and rollback

Use an appropriate stable supply and the firmware matching the board. Keep the
preceding verified image available. Start observation **before** using the local
dashboard to upload a known, signed CI artifact:

```sh
python3 tools/check_hardware.py ota-monitor http://MINER_IP --previous PREVIOUS_VERSION --expected NEW_VERSION --expect confirm --output .pio/ota-confirm.json
```

A pass requires observing the previous version, the new version with a pending
trial, then its successful confirmation. Confirmation normally needs three
minutes connected to the pool with mining active (or deliberately paused).

For a separate controlled bootloader rollback check, start the observer with
`--expect rollback`. After it observes the new pending trial, perform a physical
or serial reset **before confirmation**, using the board's proper reset control.
A dashboard restart deliberately confirms the firmware first and is not a
rollback test. The observer must see the previous version return; confirmation,
missing pending-trial evidence or timeouts never count as a rollback pass.
This needs physical/serial access and is not performed by the read-only script.

No synthetic result is substituted for these observations. Retain reports
locally and review them before treating a board/profile as hardware-validated.
