# BDLMiner V1

Solo "lottery" Bitcoin miner for the **M5StickC Plus2** and compatible ESP32 boards.
It connects to a Stratum V1 pool (`public-pool.io:21496` by default), computes
SHA‑256d hashes and, with an enormous amount of luck, may find a full block whose reward
is paid to **your** address.

> For scale: an ESP32 does ~50 to 300 kH/s, the Bitcoin network ~10^21 H/s.
> This is a lottery ticket and an educational gadget, not a source of income.

> **Status:** the firmware builds for every board below and the portable core is tested
> on PC (SHA‑256d, header assembly, a real pool session), but it has **not been tested
> on real hardware yet**. Feedback and bug reports are welcome.

## Supported boards

| PlatformIO environment | Boards | Display |
|---|---|---|
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

Hashing engines:

* **Software** on each core, optimized SHA‑256d (midstate, first 3 rounds and
  `W[16..17]` precomputed per job, early rejection at round 61).
* **Hardware** on ESP32‑S2/S3/C3 (SHA accelerator with state resume). It is
  verified at boot against the genesis block; if the check fails, software takes over.
  The original ESP32 (including the M5StickC Plus2) does not support state resume, so it
  mines in software on both cores.

## Build and flash

Requirement: [PlatformIO](https://platformio.org/) (`pip install platformio`).

```sh
pio run -e m5stickc-plus2 -t upload -t monitor   # M5StickC Plus2
pio run -e esp32-headless -t upload -t monitor   # any ESP32
pio run                                          # build all boards
```

### Headless boards

Boards without a display report everything on the serial monitor and the
[web dashboard](#web-dashboard):

* **Setup**: the serial monitor prints the access point name and password to join
  (no QR code); then follow [First-time setup](#first-time-setup).
* **Button**: BOOT (GPIO0, GPIO9 on ESP32‑C3). Hold it during boot to reopen the
  portal, or for 5 s while running to erase all settings.
* **Status LED**: steady while connecting to the pool, blinking once per second while
  mining. Only `esp32-headless` defines one by default (GPIO2); for the other boards,
  or if your LED is elsewhere, add `-DLED_PIN=<gpio>` to `build_flags`
  (plus `-DLED_ACTIVE_LOW` if it lights when the pin is low).

## First-time setup

1. On first boot the board opens a Wi‑Fi access point
   **`BDLMinerV1-XXXX`**, password **`MineYourCoins`** (boards with a display show a QR code to scan).
2. Open **http://192.168.4.1**, pick your Wi‑Fi network and fill in:
   * **Pool host / port**: `public-pool.io` / `21496` by default
     (others: `solo.ckpool.org` / `3333`, or your own pool);
   * **Backup pool host / port** (optional): used when the main pool fails, see
     [Backup pool](#backup-pool);
   * **BTC address**: your payout address, optionally followed by `.workerName`.
     It is checked at boot (Base58Check and Bech32/Bech32m checksums): if it looks
     mistyped, the screen shows *Invalid BTC address?* for 5 s and the serial monitor
     a warning. Mining still starts, since some pools use account names instead;
   * **Pool password**: usually `x`;
   * **Suggested difficulty**: share difficulty suggested to the pool, `0.0002` by default
     (the pool may enforce a higher minimum);
   * **Timezone**: POSIX TZ string, defaults to Paris time
     `CET-1CEST,M3.5.0,M10.5.0/3` (`UTC0`, `EST5EDT,M3.2.0,M11.1.0`, `JST-9`…);
   * **Price currency**: currency for the BTC price, `USD` by default
     (`USD`, `EUR`, `GBP`, `CAD`, `CHF`, `AUD`, `JPY`).
3. The board restarts and begins mining automatically. Stats can be viewed on
   https://web.public-pool.io by searching for your address.

If nothing is saved within **3 minutes**, the portal closes and the board restarts
(it reopens the portal on the next boot as long as Wi‑Fi is not configured).

To reopen the portal: **hold button A during boot** (2 s).
To erase everything (Wi‑Fi + settings): **hold A for 5 s** while running.

### Backup pool

When a backup pool is set and the main pool fails **3 times in a row** (unreachable,
or the connection drops before authorization), the miner switches to the backup with
the same address and password. After **30 minutes** on the backup it tries the main
pool again. While on the backup, the Network screen highlights it (orange pool name,
or a *Backup* label), the status reads `Mining (backup pool)` and the dashboard shows
*Pool (backup)*.

## Buttons

| | M5StickC Plus2 | T‑Display / T‑Display‑S3 | CYD | headless |
|---|---|---|---|---|
| Next screen | A (front) | right button (GPIO35 / GPIO14) | BOOT | — |
| Brightness 100→60→30→10→off | B (side) or Power | BOOT button | — | — |
| Setup portal (hold at boot) | A | right button | BOOT | BOOT |
| Reset (5 s) | A | right button | BOOT | BOOT |

Screens, in order:

1. **Mining**: hashrate, shares, best difficulty, pool difficulty, blocks, 32‑bit, temperature;
2. **Clock**: time (NTP), date, BTC price, hashrate, block height, fast fees, shares;
3. **Global**: BTC price and sats per currency unit, network hashrate, pending
   transactions, fees (fast / 30 min / 1 h), next difficulty adjustment, and the average time
   it would take *your* miner to find a block on its own;
4. **Network**: block height, next halving, network difficulty, total hashes, pool, IP/RSSI;
5. **Info**: board, firmware, engines, free RAM, address.

Clock / Global data comes from the public [mempool.space](https://mempool.space) API
(refreshed every 5 minutes over HTTPS) and the time from `pool.ntp.org`.

### Backgrounds (NerdMinerV2 style)

As on NerdMinerV2, each screen is a full-screen image with the panels and
labels already drawn; the firmware only paints the values, field by field
(a small sprite per field: no flicker, no full-screen buffer in RAM).

| Resolution | Boards |
|---|---|
| 240×135 | M5StickC Plus2 / Plus, Cardputer, T‑Display |
| 320×170 | T‑Display‑S3 |
| 320×240 | M5Stack Core / Core2 / Tough / CoreS3, CYD |

Other screen sizes (AtomS3, original StickC, portrait orientation…) keep the
code-drawn interface. Previews: `tools/preview/*.png`.

To change the design, edit `tools/make_backgrounds.py` (colors, panels, field
positions) and regenerate — the field layout is produced together with the
images, so they always stay in sync:

```sh
pip install pillow numpy
python3 tools/make_backgrounds.py
```

## Web dashboard

Once connected, the board serves a read-only status page, mostly useful for
boards without a display:

* **http://<board IP>/** or **http://bdlminer-xxxx.local/** (mDNS; `xxxx` = last digits of
  the MAC address, shown in the serial monitor);
* **/api/stats**: the same data as JSON, for Home Assistant, a script, etc.;
* **/metrics**: the main counters in Prometheus text format, for Prometheus / Grafana.

The page also shows lifetime totals and a 24‑hour hashrate chart (5‑minute averages,
kept in RAM, so it restarts empty after a reboot).

```sh
curl http://bdlminer-xxxx.local/api/stats
curl http://bdlminer-xxxx.local/metrics
```

Example response (hashrates in H/s, fees in sat/vB, `retarget.change` and
`retarget.progress` in percent, `market.updated_s` = seconds since the last
mempool.space refresh, `-1` if none yet; `history.hashrate` holds up to 288 values,
oldest first; `address_type` is `P2PKH`, `P2SH`, `P2WPKH`, `P2WSH`, `P2TR`,
`SegWit` or `invalid`):

```json
{
  "board": "M5StickC-Plus2", "firmware": "1.0.0", "uptime_s": 3600,
  "hashrate": 48120, "total_hashes": 173232000, "engines": ["sw", "sw"],
  "shares": { "accepted": 0, "rejected": 0 },
  "best": { "session": 0.0213, "ever": 0.0213 },
  "blocks": 0, "over32bits": 0,
  "pool": { "host": "public-pool.io", "port": 21496, "backup": false,
            "connected": true, "status": "Mining", "difficulty": 1 },
  "network": { "height": 972641, "difficulty": 1.27e14 },
  "address": "bc1q...", "address_type": "P2WPKH",
  "lifetime": { "hashes": 81000000000, "shares": 14, "blocks": 0 },
  "history": { "interval_s": 300, "hashrate": [47210, 48302, 48120] },
  "heap": 142000, "temp_c": 48,
  "ip": "192.168.1.20", "rssi": -61,
  "market": { "updated_s": 42, "currency": "EUR", "price": 72543,
              "fees": [3, 2, 1], "hashrate": 1.04e21, "mempool_tx": 23000,
              "retarget": { "change": 3.5, "progress": 47.1, "blocks": 1067 } }
}
```

> **Security:** the dashboard and the API have no authentication. Anyone on your local
> network can read them, including your BTC address (which is public on the pool anyway).
> They are read-only: settings can only be changed through the setup portal.

## Serial monitor (115200 baud)

```
[cfg] address type P2WPKH
[miner] hardware SHA: not supported
[miner] miner0 engine=sw core=1
[pool] job 265612f height=972641 clean=1 diff=1
[web] dashboard: http://192.168.1.20/ or http://bdlminer-a1b2.local/
[market] 5/5 ok | 1 BTC = 72543 EUR | fees 3/2/1 | net 1.04e+21 H/s | retarget +3.50% in 1067
[stats] 48.12 kH/s | shares 0 ok / 0 rejected | best 0.0213 (ever 0.0213) | blocks 0 | Mining
```

## Troubleshooting

| Symptom | What to check |
|---|---|
| Can't find the `BDLMinerV1-XXXX` network | The portal closes after 3 minutes and the board restarts; wait for it to reopen, or hold the main button during boot. |
| Portal saved but the board never mines | Read the serial monitor: Wi‑Fi password, pool host / port, and the `[pool]` lines. |
| *Invalid BTC address?* at boot | The address checksum is wrong (typo, missing character, mixed upper/lower case) or it is not a Bitcoin address. Reopen the portal and fix it; ignore the warning only if your pool uses account names. |
| Status `Auth failed: check address` | The pool rejected the user name. Reopen the portal and fix the address. |
| Status `Pool unreachable` / `Pool silent, reconnecting` | Wrong host or port, or the pool is down. Try `solo.ckpool.org` / `3333`. |
| No output in the serial monitor | Check the speed is 115200 baud. Boards with only a native USB port (ESP32‑C3 SuperMini, ESP32‑S3 / S2 mini boards) need `-DARDUINO_USB_CDC_ON_BOOT=1` in `build_flags`. |
| `bdlminer-xxxx.local` doesn't resolve | mDNS is blocked on some networks and older Android versions: use the IP shown in the serial monitor or on the Network screen. |
| Clock / Global screens stay empty | The board needs Internet access to `mempool.space` (HTTPS) and `pool.ntp.org`. The `[market]` serial line shows how many requests succeeded (`5/5 ok`). |
| `hardware SHA: self-test FAILED, disabled` on an S2/S3/C3 | The board fell back to software mining; it still works, just slower. Please report it with your board model. |

## Good to know

* `public-pool.io` currently enforces a minimum share difficulty of **1**
  (≈ 4.3 billion hashes): with an ESP32, expect several hours between two
  accepted shares. The displayed "best difficulty", however, keeps improving continuously.
* The all-time best difficulty is saved to flash (at most once per minute), the lifetime
  totals (hashes, accepted shares, blocks) every 10 minutes, and a found block at once.
  A factory reset keeps them.
* ESP32‑C6/H2: not supported by the `espressif32@6.9.0` platform (Arduino 2.x) used here.

## Architecture

```
src/
  core/            portable code (tested on PC)
    sha256.*       SHA-256, midstate, optimized mining kernel
    work.*         Stratum job -> 80-byte header, coinbase, merkle, difficulties
    stratum.*      Stratum V1 state machine (subscribe/authorize/notify/submit)
    address.*      payout address check (Base58Check, Bech32, Bech32m)
  hw_sha.*         ESP32-S2/S3/C3 hardware engine + self-test
  miner.*          FreeRTOS mining tasks, share queue, statistics, hashrate history
  net.*            network task: pool connection, reconnection, backup pool, share submission
  market.*         NTP time + mempool.space data (price, fees, hashrate, adjustment)
  web.*            HTTP dashboard + /api/stats + /metrics, mDNS
  settings.*       NVS + WiFiManager portal
  board.*          per-board display / buttons / LED / battery
  ui.*             screens (bitmap backgrounds + fields, or procedural rendering)
  bg/              generated RGB565 backgrounds + field layout
tools/
  make_backgrounds.py   background generator (Pillow)
test/host/         PC tests: ./test/host/run.sh  and  ./test/host/run.sh pool
```

PC tests (check SHA‑256d against the genesis block, header assembly, the protocol
and address validation):

```sh
./test/host/run.sh                       # offline unit tests
./test/host/run.sh pool public-pool.io 21496 <address> 900   # real pool, stops at the 1st share
```

## Credits

* The screen design (full-screen bitmap backgrounds with values painted on top) is
  inspired by [NerdMinerV2](https://github.com/BitMaker-hub/NerdMiner_v2).
* Libraries: [M5Unified / M5GFX](https://github.com/m5stack/M5Unified) (LovyanGFX),
  [WiFiManager](https://github.com/tzapu/WiFiManager),
  [ArduinoJson](https://arduinojson.org/).
* Market data: [mempool.space](https://mempool.space) public API.

## License

BDLMinerV1 is free software, released under the
[GNU General Public License v3.0](LICENSE): you may use, modify and redistribute it,
provided that modified versions you distribute are also released under the GPL‑3.0
with their source code.
