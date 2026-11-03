# Choose BTC or BCH

BDLMiner 2.1.0 supports SHA-256d pool jobs for **Bitcoin (BTC)** and **Bitcoin Cash
(BCH)**. Existing installations and complete backups without a coin field default to BTC.
The hashing kernels and ASIC protections are unchanged. This is pool mining;
BDLMiner does not run a full node or choose a chain independently of its pool.

1. Open the miner's local **Settings → Pool → Cryptocurrency** dropdown.
2. Enter the selected coin's **pool host and port**, and its **payout address or
   pool account**. Follow that pool's username/password instructions. A worker
   suffix can be appended when the pool supports it. Do not send a BTC-only
   address to a BCH pool.
3. For BCH, use **Stratum V1**. The dropdown selects V1; the server also rejects
   BCH/V2 requests. A backup pool must mine the same coin. Clear an incompatible
   backup rather than letting failover switch chain.
4. **Save and restart**. Check the displayed coin, pool authorization, supplied
   network height/difficulty, pool diagnostics and accepted shares. Switching
   coins starts a new session, like other pool-setting changes.

Changing the dropdown does not rewrite your endpoints, payout identity or pool
account. Known Bitcoin-only services (including Public Pool, Nerdminer pools and
CKPool) are refused for BCH. For custom/multi-coin pools, verify the selected
endpoint/account in the pool's own dashboard: Stratum V1 carries no authenticated
chain identifier, so the profile is not proof of the chain actually served.

## Addresses and pool requirements

- BTC: Base58Check, Bech32 and Bech32m address checks.
- BCH: standard CashAddr P2PKH/P2SH, with or without the `bitcoincash:` prefix,
  and legacy Base58Check. Checksums, mixed case, padding and hash lengths are
  checked. Prefixes are not automatically added or removed: use the exact
  username format the pool requests. Legacy BTC/BCH address encodings overlap
  and cannot identify the intended chain by themselves.
- Account-based pools remain supported. An account name is not a wallet address;
  the startup warning asks you to check the pool's requirements.

For example, [F2Pool's BCH setup guide](https://f2pool.zendesk.com/hc/en-us/articles/22893153174553-How-to-mine-Bitcoin-Cash)
documents `b4c.f2pool.com:1228`, with a **pool account** username and payout address
configured on the pool. This is a configuration example, not a tested low-difficulty
ESP32 preset. Pool endpoints, fees and minimum difficulty can change.

An ESP32 still needs a sufficiently low assigned difficulty. BCH does not make a
slow board hash faster or guarantee shares/payouts. Gamma/NerdNOS also require
enough version-rolling bits; the existing pool diagnostics enforce those ASIC
requirements. No physical BCH pool benchmark is claimed for these boards.

## What the interfaces show

The local dashboard/API and hub identify the selected coin. The physical crypto
screen shows BCH job height, network difficulty, pool and hashrate. Bitcoin price,
fees, retarget, public pool API discovery and mempool.space chain checks are
disabled for BCH; chain confirmation remains unknown rather than querying the
wrong explorer. Block proofs retain their coin across restarts and coin changes.
Lifetime hash/share counters combine the device's history across both coins.

Anonymous hashrate reports include only `BTC` or `BCH` alongside their existing
fields; the service provides a separate cryptocurrency breakdown. Old reports
without the field count under BTC. Pool-sharing and hashrate-sharing opt-outs
remain respected; no payout address, worker or private pool endpoint is sent.

Other chains and algorithms are not selectable in this release. Litecoin and
Dogecoin use Scrypt and cannot use this firmware's SHA-256d engine.
