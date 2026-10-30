# Security policy

BDLMiner V1 runs on small ESP32 boards on home networks, installs its own updates
from GitHub and keeps a WiFi password, a payout address and optional notification
and MQTT credentials. Reports that help keep that safe are very welcome.

## Supported versions

| Version | Supported |
|---|---|
| 1.8.x (latest release) | ✅ security fixes |
| older releases | ❌ — miners install the latest release by themselves (GitHub updates, setting `2`, the default) |

Fixes ship as a new release; the boards marked **experimental** (Bitaxe Gamma 601,
NerdNOS) receive the same fixes, on a best-effort basis for their hardware code.

## Reporting a vulnerability

**Please do not open a public issue, discussion or Discord message for a
vulnerability.** Report it privately through GitHub instead:

👉 **[Report a vulnerability](https://github.com/bdlglua/BDLMinerV1/security/advisories/new)**
(the repository's *Security* tab → *Report a vulnerability*).

Please include:

- the firmware version and board (`/api/stats` shows both), or the release file concerned;
- what an attacker needs (network position, the web password, physical access…);
- steps to reproduce, and what happens compared with what should;
- the impact as you understand it, and a proof of concept if you have one.

What you can expect:

- an acknowledgement within **7 days**;
- an assessment and a plan within **14 days**;
- a fixed release, then a published advisory crediting you (unless you prefer not to
  be named). Please keep the details private until the fixed release is out, or
  90 days, whichever comes first; we can agree on another date together.

## Scope

In scope:

- the firmware: the local web server and its API, settings, backup and restore,
  firmware upload, GitHub update discovery, download and **signature verification**,
  the firmware trial and rollback, Stratum V1, Stratum V2 with Noise, notifications
  (ntfy, webhook), MQTT / Home Assistant;
- the release pipeline: release signing, `update.json`, the published files, and the
  web installer at <https://bdlglua.github.io/BDLMinerV1/>.

Out of scope:

- anything that requires physical or USB access to the board (it can always be
  re-flashed), or knowledge of the web password, except a way around it;
- denial of service by flooding the miner's small HTTP server from the local network;
- the pools, mempool.space, ntfy, Discord, Telegram or other third-party services;
- vulnerabilities in dependencies without an impact shown in BDLMiner (please report
  them upstream as well);
- social engineering, spam, and reports from automated scanners without a
  demonstrated issue.

## Security model and known limitations

These are deliberate design choices for a device on a home network, documented so
that reports can focus on what goes beyond them:

- **The dashboard is plain HTTP on the local network.** The web password uses HTTP
  Basic authentication, which an attacker on the same network can read. Without a
  web password, settings, backups, firmware upload, restarts and crash reports
  cannot be used at all from the web; only checking for and installing the latest
  **signed** GitHub release stays available (with a password set, it asks for it).
- **Read-only pages need no password**: the dashboard, `/api/stats`, `/api/log`,
  `/api/history` and the live stream show the payout address, pool and network
  details to anyone on the local network. `/api/stats` allows cross-origin reads so
  that one miner's dashboard can show the others.
- **Firmware from GitHub is trusted by its signature, not by TLS.** Every release
  firmware is signed with an ECDSA P-256 key kept outside the repository; the miner
  checks the signature against the public key built into it (`src/update_key.h`)
  before installing anything, then runs the new version on trial and returns to the
  previous one if it fails.
- **HTTPS connections do not verify certificates** (market data, pool statistics,
  GitHub metadata, ntfy and webhook notifications), to fit the memory of the board.
  An active attacker on the path could read or alter that data, including a webhook
  URL or ntfy topic, which act as secrets. Firmware stays protected by its signature.
- **A firmware uploaded through the web page is not signature-checked**, so that
  people can install their own builds: the web password is what protects it.
- **MQTT is plain TCP**; its user and password travel unencrypted to the broker.
- **Crash reports are memory images** and may contain secrets: downloading or erasing
  them requires the web password.

## Keeping your miner safe

- Set a **web password** in the setup portal, and keep it different from your WiFi password.
- Keep the miner on your home or IoT network; **never forward its port 80** to the internet.
- Leave **GitHub updates** on, so security fixes reach the miner by themselves.
- Treat your **webhook URL**, **ntfy topic** and **backup files** as secrets: a backup
  contains every setting and password.
