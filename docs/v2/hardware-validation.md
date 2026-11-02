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
| Gamma OTA/stability | Confirmed beta.3 currently running; full transition not observed. Complete an hour-long counter/health check, then observe beta.4 OTA. |
| Rollback/network recovery | Procedures and observers ready; physical results still required. |
| S2/S3/C3 and NerdNOS | No ordinary test boards available to the owner; physical measurements pending. |

## Gamma hour while preparing the M5

In the current beta.3 kit, in a separate terminal:

```sh
python3 tools/hardware_suite.py --config bdlhub.local.json --miner gamma --seconds 3600 --output gamma-stability-beta3.json
```

Keep Gamma powered, connected and on this version until it finishes. Do not run
Gamma OTA or a shared-network interruption during this hour. M5 work can proceed
independently. A beta.3 result is not a beta.4 stability measurement.

## Extract the beta.4 kit

Download `v2-linux-test-kit` from the successful `v2/development` CI run. Extract
into a new folder; retain the existing kit/settings backup. Check `kit.json`
reports `2.0.0-beta.4` and run `sha256sum -c SHA256SUMS.txt`.
Images are unsigned/manual OTA previews. Only `<board>-firmware.bin` goes through
the local dashboard; `<board>-factory.bin` is for USB installation/recovery.

## M5 rollback first, then normal confirmation

Identify the M5 USB-UART port with `ls -l /dev/serial/by-id/`. The Linux helper
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
python3 tools/monitor_network.py --config bdlhub.local.json --miner m5 --kind wifi --output network-m5-beta4.json
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

## Gamma OTA after its hour

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
