# Choose a SHA-256d cryptocurrency

BDLMiner 2.4.0 offers six pool profiles, including Peercoin proof of work. Existing BTC/BCH IDs and settings are
preserved; older records without a coin field default to BTC.

| Profile | Protocols | Recognized payout address formats |
| --- | --- | --- |
| Bitcoin (BTC) | Stratum V1 or native V2 | Bitcoin Base58Check, Bech32/Bech32m |
| Bitcoin Cash (BCH) | Stratum V1 | `bitcoincash:` CashAddr (prefix optional), legacy Base58Check |
| Bitcoin SV (BSV) | Stratum V1 | Legacy Base58Check, according to the pool's payout policy |
| eCash (XEC) | Stratum V1 | `ecash:` CashAddr (prefix optional), legacy Base58Check |
| DigiByte SHA-256d (DGB) | Stratum V1, SHA-256d port only | DigiByte Base58Check, `dgb1` Bech32/Bech32m |
| Peercoin (PPC), proof of work | Stratum V1 | Peercoin Base58Check (`P`/`p`), `pc1` Bech32/Bech32m |

Account-based pools remain supported. The payout/username field is not required
to be a wallet address when the pool asks for an account name.

1. Open local **Settings → Pool → Cryptocurrency** and choose the profile.
   The first-setup portal uses IDs `0` BTC, `1` BCH, `2` BSV, `3` XEC, `4` DGB and `5` PPC.
2. Enter that coin's **pool host and port**, and its **payout address or pool
   account**. Follow the pool's worker suffix, password and address-prefix rules.
3. Non-BTC profiles select **Stratum V1**. Any backup must serve the same coin;
   clear an incompatible backup before saving. For DGB, select **SHA-256d** on
   the pool, not its Scrypt, Skein, Qubit or Odocrypt service.
4. **Save and restart**. Check the coin, authorization, job height/difficulty,
   pool diagnostics and accepted shares in both the local and pool dashboards.

The selector does not rewrite pool endpoints, convert addresses, or change a
pool account's coin selection. Known BTC-only services are refused for the
other profiles; recognized coin-specific endpoints cannot be selected under a
different profile. Custom/multi-coin endpoints remain the operator's
responsibility: Stratum V1 has no authenticated chain identifier. The profile
identifies the configured choice, rather than proving the chain served.

DigiByte headers identify their algorithm. The network session and mining engine
reject non-SHA-256d jobs and masks that would roll its algorithm bits. This keeps
an accidental Scrypt connection from appearing to perform valid DGB mining.

## Peercoin proof of work

Select **Peercoin (PPC) · PoW** and configure a Peercoin Stratum V1 pool. This
mines Peercoin's SHA-256d proof of work; it does not perform proof-of-stake
minting, run a wallet or require a private key. The pool constructs the coinbase
and assembles/signs the final block where the chain requires it. Mining directly
against a Peercoin wallet RPC is not supported.

- [Peermining setup](https://peermining.org/site/mining/PPC) currently documents
  host `peermining.org`, port `3333`, username `YOUR_PPC_ADDRESS.worker` and
  password `x`. Check its current instructions and minimum share difficulty
  before use; this is not a measured ESP32/Gamma preset.
- [Mining-Dutch PPC setup](https://www.mining-dutch.nl/pools/peercoin.php) uses a
  shared SHA-256 service: select **PPC in the pool account** as well as in the
  miner. A matching host alone cannot identify the chain served.

Peercoin mainnet addresses use Base58Check versions 55/117 or the `pc` SegWit
prefix. Testnet uses 111/196 or `tpc`, regtest uses `pcrt`, and signet shares `tb`
with Bitcoin. Test-network legacy encodings also overlap. Payout format support
is ultimately determined by the chosen pool.

The pool supplies the non-witness coinbase bytes for the transaction ID, just
as with Bitcoin Stratum. They are hashed unchanged, including `nTime` for
Peercoin transaction versions below 3; the height reader accounts for that
extra field. Transaction version 3 omits it. The proof-of-work header remains
80 bytes, hashed twice with SHA-256; Peercoin's P2P header flags are excluded.
Share difficulty follows the SHA-256d Stratum difficulty supplied by the pool;
Peercoin's network target comes directly from each job's `nBits`. No Bitcoin
halving, market data or block-confirmation lookup is shown for PPC.

## Pool and hardware requirements

All profiles use the same SHA-256d engine. Changing coins does not increase a
board's speed. ESP32 devices still need low assigned difficulty; Gamma/NerdNOS
also require enough version-rolling bits. Existing pool diagnostics and hardware
protections remain active. Physical BSV/XEC/DGB/PPC pool measurements are pending;
software compatibility tests are not hardware benchmarks.

Use the services' current documentation to choose endpoints, ports and account
settings. The following are configuration references, not tested low-difficulty
presets or guarantees about payouts:

- [BSV mining at Mining-Dutch](https://www.mining-dutch.nl/pools/bitcoinsv.php):
  the account/dashboard selects the coin on its shared SHA-256 service.
- [eCash XEC mining](https://e.cash/mining): official service list and requirements
  for pool operators. The pool builds the required coinbase/miner-fund outputs.
- [DigiHash setup](https://digihash.digibyte.io/getting_started): select SHA-256 and
  the appropriate difficulty port; its other algorithms are incompatible.
- [F2Pool BCH setup](https://f2pool.zendesk.com/hc/en-us/articles/22893153174553-How-to-mine-Bitcoin-Cash):
  an example of account-based BCH configuration.

Checksums and chain-specific prefixes are checked. BCH/XEC CashAddr checks cover
standard P2PKH/P2SH payloads; prefixes are not automatically added or removed.
Legacy encodings shared between chains cannot identify the intended network on
their own. Address validation checks encoding, not a pool's spending/payout policy.

## Displays, statistics and backups

The dashboard/API, physical crypto display and hub identify the selected coin.
Non-BTC profiles show pool-supplied job information; Bitcoin price, fees,
retarget, pool API discovery and mempool.space confirmation are disabled. DGB/PPC
do not show Bitcoin's 210,000-block halving counter. Saved proofs retain their
coin and are never resubmitted after restart. Lifetime counters combine device
history across all selected coins.

Anonymous reports use the fixed labels BTC/BCH/BSV/XEC/DGB/PPC, with separate public
totals. Old reports without a ticker count under BTC. Sharing opt-outs and private
endpoint masking are preserved; no address, account, worker or miner identifier
is added.

New-profile NVS records use a new record marker that older firmware rejects, so
it cannot silently reinterpret a new profile as BTC after rollback. BTC/BCH
records keep their existing format. An older firmware may load an earlier
compatible configuration or request setup again. JSON backups still export the
numeric setting; importing an unsupported coin into older firmware is refused.

The native user-agent option follows the new firmware version (for example,
`BDLMinerV2/2.4.0`); an explicit `NerdMinerV2` choice is retained.

Other algorithms and profiles are not selectable. Merged-mining chains need
pool-specific AuxPoW handling and are not advertised as independent profiles.

Address and header references:
[Bitcoin ABC](https://github.com/Bitcoin-ABC/bitcoin-abc/tree/master/modules/ecashaddrjs),
[BSV chain parameters](https://github.com/bitcoin-sv/bitcoin-sv/blob/master/src/chainparams.cpp),
[DigiByte parameters](https://github.com/DigiByte-Core/digibyte/blob/develop/src/kernel/chainparams.cpp),
[DigiByte header algorithm bits](https://github.com/DigiByte-Core/digibyte/blob/develop/src/primitives/block.h).

Peercoin references: [mining documentation](https://www.peercoin.net/docs/mining),
[chain parameters and genesis vector](https://github.com/peercoin/peercoin/blob/44f5adac80c891a3f215562d69e1b1579873600e/src/kernel/chainparams.cpp),
[transaction serialization](https://github.com/peercoin/peercoin/blob/44f5adac80c891a3f215562d69e1b1579873600e/src/primitives/transaction.h),
[header hashing](https://github.com/peercoin/peercoin/blob/44f5adac80c891a3f215562d69e1b1579873600e/src/primitives/block.h).
