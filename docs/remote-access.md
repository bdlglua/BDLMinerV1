# Private remote monitoring

[Français](remote-access.fr.md)

View your miners from a phone or another network at the
**[private remote dashboard](https://bdlglua.github.io/remote/)**.
The boards send readings directly over verified HTTPS: no computer, Raspberry Pi,
VPN, port forwarding or local hub needs to stay running. The miners need Wi-Fi
with Internet access and **BDLMiner 2.3.0 or newer**.

## Pair your miners

1. Install the latest signed stable update from the local dashboard, or upload
   the matching board firmware from [Releases](https://github.com/bdlglua/BDLMinerV2/releases).
2. Open the remote dashboard and choose **Create my key**. Keep a copy in your
   password manager. **Copy key** copies the 64-character monitoring key.
3. Open each miner's local page. In **Settings → Network → Private remote
   monitoring**, paste the same key, choose a name such as `M5 bureau` or
   `Gamma atelier`, and enable monitoring. **Save and restart** once.
4. On the remote page, paste your key and choose **Connect**. The first report
   is due about one minute after boot; later reports arrive every five minutes.
   A missing clock synchronization, Wi-Fi connection or TLS memory delays an
   attempt. See the miner's log for `[remote]` messages.

Pairing is optional and disabled by default. It works independently of anonymous
hashrate/pool sharing: turning one service off does not change the other.

## What you can see

Each card shows the miner's chosen name, board, firmware, cryptocurrency,
hashrate, temperature when available, accepted/rejected shares, uptime, best
difficulty, effective mining power and pool connection/public group. Paused
miners send a zero rate; disconnected pools are shown separately.

Readings and chart history are kept for **seven days**. History windows are one,
six or 24 hours and seven days. Missing reports and changed boot IDs make chart
gaps. A miner is considered online while its last report is at most **11 minutes**
old; the age is shown on its card. An offline miner's last known rate is retained
on its card and excluded from the total online rate. This is periodic telemetry,
not a continuous connection or a confirmation of pool payment/block discovery.

The viewer has no commands to change settings, restart, calibrate or update a
miner. Those operations stay on the local miner page.

## Keys, privacy and removal

- The browser creates a random **256-bit** key. Anyone with the key can view and
  erase that private space. Use **Copy my key** while connected to keep a copy;
  the service cannot recover a lost key.
- The browser keeps the key in memory only, sends it in an authorization header,
  and clears readings on **Disconnect**. It is never put in links, browser
  storage, analytics or the public statistics. Reloading asks for it again.
- On the miner, the key uses the existing secret-setting storage and masking.
  As with other secrets, protected backups can contain it. Naming or pairing a
  miner requires a restart; the enable/disable switch applies immediately.
- Personal reports contain a keyed pseudonymous device ID and the chosen name.
  They never contain a payout address, worker, raw MAC, LAN address, Wi-Fi
  credentials, web password or configured private pool host. A recognized public
  pool is represented by a fixed group; other pools are `other`.
- Reports use CA and hostname verification, with no insecure fallback and no
  redirects. A failed report waits for the next attempt; it does not restart the
  miner or stop mining. The public statistics retain their existing behavior.
- The Cloudflare-hosted service isolates each key's data in its own Durable
  Object using a SHA-256 digest as its name. The raw key is not persisted. The
  hosting operator can access stored readings; this is authenticated private
  storage, not end-to-end encryption. Request addresses are used only for
  temporary in-memory rate limits.
- To stop sending, disable monitoring on each miner. Existing readings expire
  after seven days. To remove them immediately, choose **Manage my private data →
  Erase data and revoke key** on the remote page. This permanently revokes the
  old key; a minimal revocation marker remains so reports cannot recreate it.
  Create a new key and change it on every miner if you want to resume.

A private space supports **32 miners**. The backend accepts at most one report
per miner per minute; the firmware normally sends one every five minutes. A
temporary admission limit also bounds requests from one Internet address.

## Private API

The same key can be used as `Authorization: Bearer <personal-key>` on:

- `GET /api/remote`: your latest readings and their age.
- `GET /api/remote/history?miner=<id>&hours=24`: up to about 480 chart buckets;
  `hours` accepts `1`, `6`, `24` or `168`.
- `DELETE /api/remote`: erase your space and permanently revoke its key.

These endpoints are on `https://bdlminer-stats.bdlminer-bot.workers.dev`,
require the key even when public statistics are open, and use `Cache-Control:
no-store`. Browser reads are allowed only from the GitHub Pages viewer origin
(and the legacy service origin); no credentials are sent automatically. `POST /api/remote/report` is used only by paired miners and is
strictly separated from the anonymous `/report` endpoint.
