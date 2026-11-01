# ASIC version-rolling negotiation in Stratum V2

The owner's Gamma screenshot under 1.9.14 shows a connected Public Pool V2
endpoint at zero current hashrate and **Pool needs version rolling**. The chart
retains earlier mining history. No live private-network access was available;
the screenshot does not expose the actual wire frames or the pool's setup flags.

An ASIC-configured native probe reproduced a definite client error: the actual
`SetupConnection` frame sends flags 0. Gamma requires at least eight rolling
bits, and NerdNOS at least two, so neither can promise to mine fixed-version
jobs. The parser previously accepted `version_rolling_allowed: false` as a zero
mask, leaving the backend to stop while the network session counted that work
as healthy. This is consistent with the reported symptom.

[Mining protocol section 5.3.1](https://github.com/stratum-mining/sv2-spec/blob/main/05-Mining-Protocol.md#531-setupconnection-flags-for-mining-protocol)
defines request bit 2 as `REQUIRES_VERSION_ROLLING`. A successful setup commits
the server to that requirement; the server must not send jobs forbidding rolling.
Success flags use a different namespace: bit 0 requires a fixed version, bit 1
requires extended channels, and bit 2 is not a valid success flag.

The corrective build defaults to request flag 4 for both ASIC configurations.
Software ESP32 requests remain 0. A fixed-version setup, unsupported rolling flag
or later non-rolling job produces a version-rolling diagnostic before invalid
work is published. A later incompatible job clears the session's usable-work
result so the existing configured-backup policy can run. No implicit protocol
downgrade, invented version mask or unverified share is introduced.

Regression coverage uses the production protocol plus the actual Noise/session
with both Gamma and NerdNOS build definitions. An independent Python responder
checks wire flags, fixed-version/unsupported-feature/first-job refusals, two
genuine rolled-version SHA-256d proofs, their acknowledgments and a subsequent
job losing rolling permission. The software session and fixed-version handling
remain covered. These tests use local sockets, not a public pool or physical ASIC.

The initial hardware probe sent flag 0; the same probe after correction sends
flag 4. End-to-end loopback tests passed. A real Public Pool trial of this new
build is still needed: a server may refuse the required feature, in which case
the firmware must report incompatibility instead of manufacturing rolling bits.
The documented 1.9.14 workaround is an explicit compatible V1 endpoint such as
`public-pool.io:3333`, preserving the owner's address and chosen power settings.
The owner subsequently confirmed that this V1 change restored Gamma mining.

The complete native suite and 24 host-tool tests passed after the correction.
Both actual ASIC targets compiled successfully (`bitaxe-gamma-601`, `nerdnos`).
The next development version is 1.9.15; its physical V2 behavior on Public Pool
is not inferred from the local peer tests or the V1 recovery report.
