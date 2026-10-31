# BDLMiner V1

Solo "lottery" Bitcoin miner for the **M5StickC Plus2** and compatible ESP32 boards.
It computes SHA‑256d hashes and connects to a pool using **Stratum V1 or native
Stratum V2**. With an enormous amount of luck, it may find a full Bitcoin block
whose reward is paid to **your** address.

**Latest release: [v1.9.6](https://github.com/bdlglua/BDLMinerV1/releases/tag/v1.9.6)**
· [Site and web flasher](https://bdlglua.github.io/BDLMinerV1/)
· [Release notes](docs/releases/v1.9.6.md)
· [User guide](docs/guide.md)
· [Statistics](https://bdlminer-stats.bdlminer-bot.workers.dev/)
· [Discord](https://discord.gg/S8kSgD6Xe9)

Version **1.9.6** fixes the bugs of a full audit: the miner's pages refuse requests from
other sites, never show the passwords and load without stalls. Earlier versions are
described in [docs/releases](docs/releases/).

> For scale: an ESP32 does ~50 kH/s in software and up to ~1 MH/s with its hardware SHA
> engine, the Bitcoin network ~10^21 H/s.
> This is a lottery ticket and an educational gadget, not a source of income.

## Supported boards

| PlatformIO environment | Boards | Display |
|---|---|---|
| `bitaxe-gamma-601` **experimental** | Bitaxe Gamma **601**, one BM1370, ESP32-S3 N16R8 | SSD1306 OLED 128×32 |
| `nerdnos` **experimental** | [NerdNOS](docs/hardware/nerdnos.md): one BM1397 on a board carrying a T‑Display‑S3 | ST7789 170×320 (parallel) |
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

**The Bitaxe Gamma 601 and NerdNOS targets have not been validated on physical boards.**
Read their guides first: [Gamma 601](docs/hardware/bitaxe-gamma-601.md) (replaces AxeOS;
5 V ≥4 A barrel supply and active cooling) and [NerdNOS](docs/hardware/nerdnos.md).

## Install

Open **https://bdlglua.github.io/BDLMinerV1/** in Chrome or Edge on a computer, plug the
board in with a data USB cable, pick it in the list and click *Install* (it flashes the
latest release; the first install erases the board). Each
[release](https://github.com/bdlglua/BDLMinerV1/releases) also has a
`<board>-factory.bin` (first install, at 0x0) and a `<board>-firmware.bin` (update): see
[Install a release](docs/guide.md#install-a-release-no-build-tools). To build it
yourself, see [development.md](docs/development.md).

## First-time setup

1. On first boot the board opens a Wi‑Fi access point **`BDLMinerV1-XXXX`**, password
   **`MineYourCoins`** (boards with a display show a QR code to scan).
2. Open **http://192.168.4.1**, pick your Wi‑Fi network and enter your **pool**
   (see below), your **Bitcoin address** (optionally `address.worker`) and a **web
   password** (needed to change settings from the dashboard).
3. The board restarts and mines. Its dashboard is at **http://bdlminer-xxxx.local/** or
   the IP shown on the Network screen.

Every portal field is described in the [user guide](docs/guide.md#first-time-setup).
Hold the main button during boot to reopen the portal, or 5 s while running to erase
everything.

### Pools for ESP32

For visible feedback at ESP32 hashrates, start with a pool that accepts low-difficulty
shares. These are solo pools: accepted shares track your work; a payout requires
finding a valid Bitcoin block.

| Pool | Host | Port | Protocol / setup |
|---|---|---|---|
| [Nerdminers.org](https://pool.nerdminers.org/) | `pool.nerdminers.org` | `3333` | V1; designed for NerdMiner and similar ESP32 devices. Leave user agent on Automatic. |
| [NerdMiner.io](https://nerdminer.io/) | `pool.nerdminer.io` | `3333` | V1; announces adaptive difficulty for small miners. Use your valid Bitcoin address as the worker. |
| [Public Pool solo](https://web.public-pool.io/) | `public-pool.io` | `3333` | V1. |
| [Public Pool solo](https://web.public-pool.io/) | `public-pool.io` | `23330` | V2, with the authority key below. |

For Nerdminers.org, use `YOUR_BTC_ADDRESS.worker1`; for NerdMiner.io, follow its
published worker instructions and use your Bitcoin address. Password `x` is the
usual V1 setting. Check the pool's linked setup page if its connection details
change. The firmware's compiled default remains `public-pool.io:21496`; applying
a preset changes the saved endpoint to the values shown above.

A low suggested difficulty is a request, not a guarantee. The assigned pool
or channel difficulty determines how often accepted shares can appear; see
[Pool shares and local records](docs/guide.md#pool-shares-and-local-records).

## Using it

* **Screens and buttons**: eight pages (hashrate, clock, Bitcoin, network, device,
  graph, records, blocks), one big number each; see
  [Buttons and screens](docs/guide.md#buttons-and-screens).
* **Web dashboard**: live statistics, what the pool sees, a 24-hour chart, settings
  (most apply without a restart), backups, firmware upload and the log, plus
  `/api/stats` and Prometheus `/metrics`; see [Web dashboard](docs/guide.md#web-dashboard).
* **Updates**: new releases install by themselves from GitHub, only with a valid
  signature, and run on trial with automatic rollback; see
  [Updates from GitHub](docs/guide.md#updates-from-github).
* **Notifications**: ntfy, a webhook (Discord, Telegram…), alerts and a daily summary;
  see [Notifications](docs/guide.md#notifications).
* **Home Assistant**: MQTT discovery with controls; see
  [Home Assistant](docs/guide.md#home-assistant).
* **Safety**: mining power, heat limits, battery saver and watchdogs; see
  [Safety, power and networks](docs/guide.md#safety-power-and-networks).
* **Backup pool, Stratum V2, user agent**: see the [user guide](docs/guide.md).

### Anonymous usage count

To know how many miners run each version, 5 minutes after boot and then once a day the
miner downloads a 2-byte file of its own version's release, `ping-<board>.txt`. GitHub
counts the downloads of each release file, and `python3 tools/miner_stats.py` turns these
counts into active miners per version and board. Nothing about the miner is sent (no
address, network or identifier: only GitHub sees the download, like any other). It is on
by default; turn it off with the *Anonymous usage count* setting (`count`).

### Anonymous hashrate sharing

To know the total hashrate of the BDLMiner miners, every 5 minutes (the first time 3
minutes after boot) the miner posts its firmware version, its board, its hashrate (the
average of the last minute, rounded to 3 digits), the interval until its next report and,
on M5Stack boards, their exact model (such as `M5Stack Core2`) to the
[BDLMiner statistics service](https://github.com/bdlglua/BDLMiner-stats):

```json
{"v": "1.9.6", "board": "m5stack-esp32", "hashrate": 1.02e6, "every": 300, "model": "M5Stack Core2"}
```

Nothing else is sent: no Bitcoin address, worker, pool, name or identifier. The service
adds the reports up live and keeps a 5-minute sample of the total 35 days; the sender's network
address only limits the number of reports and is never stored. The totals are public:
**<https://bdlminer-stats.bdlminer-bot.workers.dev/>** (total hashrate, miners, 7-day
chart, by board and version). A paused miner sends
nothing. It is on by default; turn it off with the *Share my hashrate anonymously*
setting (`sharehr`), which applies at once.

## Troubleshooting

| Symptom | What to check |
|---|---|
| Can't find the `BDLMinerV1-XXXX` network | The portal closes after 3 minutes and the board restarts; wait for it to reopen, or hold the main button during boot. |
| Portal saved but the board never mines | Read the serial monitor: Wi‑Fi password, pool host / port, and the `[pool]` lines. |
| *Invalid BTC address?* at boot | The address checksum is wrong (typo, missing character, mixed upper/lower case) or it is not a Bitcoin address. Reopen the portal and fix it; ignore the warning only if your pool uses account names. |
| Status `Auth failed: check address` | The pool rejected the user name. Reopen the portal and fix the address. |
| Status `Pool unreachable` / `Pool silent, reconnecting` | Check the host, pool availability and the port for the selected protocol. A V1 port and a V2 port are not interchangeable; see [Pools for ESP32](#pools-for-esp32). |
| Hashrate is visible but accepted shares stay at zero | Check the assigned pool difficulty, connection status, pending replies and activity log. Local records do not require pool acceptance; see [Shares explained](docs/guide.md#pool-shares-and-local-records). |
| M5StickC Plus2 slows down with the local dashboard open | v1.6.3 reduces HTTP/page overhead and refreshes statistics every 10 seconds. See the [dashboard-load review](docs/audits/2026-10-27-m5-dashboard-load.md). A hardware hashrate gain has not yet been measured. |
| `SV2 authority key missing/invalid` or `SV2 server verification failed` | Verify the pool's published authority key and V2 port. Pasting the pool's `stratum2+tcp://host:port/key` address fills both; an altered or expired server certificate is refused. |
| `SV2 waiting for synchronized time` | Allow the miner to reach its NTP time server before certificate verification can complete. |
| A manually selected user agent is refused | Enter an identity accepted by that pool or select Automatic. Manual mode preserves your choice rather than replacing it after a refusal. |
| No output in the serial monitor | Check the speed is 115200 baud. Boards with only a native USB port (ESP32‑C3 SuperMini, ESP32‑S3 / S2 mini boards) need `-DARDUINO_USB_CDC_ON_BOOT=1` in `build_flags`. |
| `bdlminer-xxxx.local` doesn't resolve | mDNS is blocked on some networks and older Android versions: use the IP shown in the serial monitor or on the Network screen. |
| Clock / Global screens stay empty | The board needs Internet access to `mempool.space` (HTTPS) and `pool.ntp.org`. The `[market]` serial line shows how many requests succeeded (`5/5 ok`). |
| `hardware SHA: self-test FAILED, disabled` | The board fell back to software mining; it still works, just slower. Please report it with your board model and the `[miner]` lines. |

## Development

```sh
pio run -e m5stickc-plus2 -t upload -t monitor   # build and flash the M5StickC Plus2
./test/host/run.sh                               # host tests
```

[development.md](docs/development.md) covers building every board, the hashing engines,
releases and the code layout; [CONTRIBUTING.md](CONTRIBUTING.md) how to contribute. Security
problems: see [SECURITY.md](SECURITY.md).

## Credits

* Native Gamma 601 board configuration, BM1370 register/packet sequences and
  TPS546/EMC2101 references derive from [ESP-Miner / AxeOS](https://github.com/bitaxeorg/ESP-Miner)
  (GPL-3.0), revision `bb4fd0c7e589858bbeef00ec89342216c1510671`, and
  [bitaxeGamma hardware](https://github.com/bitaxeorg/bitaxeGamma).
* NerdNOS support: BM1397 packets, PLL calculation and start-up sequence, and the
  board's pins, derive from ESP-Miner (above) and the NerdNOS firmware in
  [WantClue/NerdMiner_v2](https://github.com/WantClue/NerdMiner_v2/tree/nerdnos)
  (branch `nerdnos`, revision `22588347597d24969f41ed56fad2641678ad59c1`, GPL-3.0); the board is
  [benjamin-wilson/NerdNOS](https://github.com/benjamin-wilson/NerdNOS).

* The earlier bitmap screen design was inspired by
  [NerdMinerV2](https://github.com/BitMaker-hub/NerdMiner_v2).
* The original-ESP32 hardware SHA loop (`src/hw_sha_classic.S`) and the assembly
  software kernel (`src/sw_sha_prefilter.S`) come from
  [NerdMiner Modded](https://github.com/HenrysCat/NerdMiner-Modded) by HenrysCat,
  MIT License, Copyright (c) 2023 Bitmaker, (c) 2026 HenrysCat.
* Libraries: [M5Unified / M5GFX](https://github.com/m5stack/M5Unified) (LovyanGFX),
  [WiFiManager](https://github.com/tzapu/WiFiManager),
  [ArduinoJson](https://arduinojson.org/).
* V2 cryptography: [Bitcoin Core libsecp256k1](https://github.com/bitcoin-core/secp256k1)
  (MIT, pinned source in `lib/secp256k1`) and
  [Monocypher](https://github.com/LoupVaillant/Monocypher) (BSD-2-Clause, pinned source
  in `lib/monocypher`). Their upstream notices and exact revisions are retained.
* Stratum V2 protocol: [official specification](https://stratumprotocol.org/).
* Market data: [mempool.space](https://mempool.space) public API.

## License

BDLMinerV1 is free software, released under the
[GNU General Public License v3.0](LICENSE): you may use, modify and redistribute it,
provided that modified versions you distribute are also released under the GPL‑3.0
with their source code.
