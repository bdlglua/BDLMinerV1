# Bitcoin pools for ESP32

These endpoints are for **BTC**. For BCH, BSV, XEC or DigiByte SHA-256d, use a
matching pool and account as described in [SHA-256d configuration](mining-coins.md).

For visible feedback at ESP32 hashrates, start with a pool that accepts low-difficulty
shares. These are solo pools: accepted shares track your work; a payout requires
finding a valid Bitcoin block.

| Pool | Host | Port | Protocol / setup |
|---|---|---|---|
| [Nerdminers.org](https://pool.nerdminers.org/) | `pool.nerdminers.org` | `3333` | V1; designed for NerdMiner and similar ESP32 devices. Select the `NerdMinerV2` user agent. |
| [NerdMiner.io](https://nerdminer.io/) | `pool.nerdminer.io` | `3333` | V1; announces adaptive difficulty for small miners. Use your valid Bitcoin address as the worker. |
| [Public Pool solo](https://web.public-pool.io/) | `public-pool.io` | `3333` | V1. |
| [Public Pool solo](https://web.public-pool.io/) | `public-pool.io` | `23330` | V2; see [authority-key setup](guide.md#stratum-v1-and-stratum-v2). |

For Nerdminers.org, use `YOUR_BTC_ADDRESS.worker1`; for NerdMiner.io, follow its
published worker instructions and use your Bitcoin address. Password `x` is the
usual V1 setting. Check the pool's linked setup page if its connection details
change. The firmware's compiled default remains `public-pool.io:21496`; applying
a preset changes the saved endpoint to the values shown above.

A low suggested difficulty is a request, not a guarantee. The assigned pool
or channel difficulty determines how often accepted shares can appear; see
[Pool shares and local records](guide.md#pool-shares-and-local-records).

[Back to the project overview](../README.md#pools-for-esp32).
