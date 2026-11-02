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

The initial ASIC-configured native probe sent flag 0; the same probe after
correction sends flag 4. End-to-end loopback tests passed. Those tests alone
could not establish live Public Pool compatibility. A server may refuse the
required feature, in which case the firmware must report incompatibility
instead of manufacturing rolling bits.
The documented 1.9.14 workaround is an explicit compatible V1 endpoint such as
`public-pool.io:3333`, preserving the owner's address and chosen power settings.
The owner subsequently confirmed that this V1 change restored Gamma mining.

The complete native suite and 24 host-tool tests passed after the correction.
Both actual ASIC targets compiled successfully (`bitaxe-gamma-601`, `nerdnos`).
The development version is 1.9.15. The complete
[CI run](https://github.com/bdlglua/BDLMinerV2/actions/runs/41345688410) passed all
twelve board builds plus native/tool and browser tests. The Gamma development
artifact was independently checked against its GitHub ZIP digest, binary
checksums, ESP32-S3 image checksum/appended SHA, version/model markers, matching
factory app and two fitting OTA slots. It is a manual test build, not a signed
published release. Source commit: `db67b2123b1e784b93e7f079084a7e4ff8a93043`.

## Owner-reported Gamma V2 confirmation

On 1 November 2026 at 20:30:17 UTC (21:30 Europe/Paris), the owner supplied a
completed, non-simulated tool-version-3 report. It observes confirmed 1.9.14,
a new 1.9.15 boot in a pending trial 75 times, then that same 1.9.15 boot
confirmed. The result is `confirmed`, with `saw_pending_trial` and `passed`
both true. The final sample explicitly reports **Stratum V2 (Noise)**, not V1.

| Reading | Owner-reported value |
| --- | --- |
| ASIC / frequency | BM1370 running, 525 MHz |
| Rolling / live hashrate | 1.093021 / 1.166250 TH/s |
| ASIC / regulator temperature | 53 / 57.5 C |
| Fan | Automatic, 78%, 8,667 RPM; no override |
| Input / core voltage | 5.242188 / 1.144531 V |
| Core-side current | 14.04688 A |
| Regulator status / hardware errors | 0 / 0.039573% |
| Session shares | 9 accepted, 0 rejected, 0 pending |
| Effective power / configured limit | 100%, no limitation / 65 C |
| Stock fallback | None |

This owner-run observation establishes successful OTA confirmation and resumed
Gamma mining with accepted shares over the intended V2 connection after the
client correction. Transient timeout/reset errors precede the new boot; their
individual causes cannot be inferred from the report. The new boot remains
unchanged through the observed trial. No cloud-to-device measurement is claimed.

The last assigned difficulty is 16,384 and credited work is 81,661.6889. The
report does not include each share's assigned target; vardiff can change during
the run, so credited work must not be compared with nine times the final target.
The short rolling/live rates are observations, not evidence of increased ASIC
clock speed or a sustained performance gain. Prolonged stability, physical
NerdNOS validation of this patch and operation on other V2 pools remain outside
this owner's Gamma trial. The public release is still 1.9.14 at this point.

## Publication completed

Following the owner's explicit request, annotated tag `v1.9.15` was pushed at
`660a25f23332312669830d9dbe63355d02dd89d7`. Its firmware and build inputs match
the owner-tested corrective source at `db67b2123b1e784b93e7f079084a7e4ff8a93043`;
intervening changes are documentation only. The normal workflow rebuilt and
signed all twelve variants, then published
[v1.9.15](https://github.com/bdlglua/BDLMinerV2/releases/tag/v1.9.15) on
1 November 2026 at 21:54:03 Europe/Paris.

Independent public downloads verified all 51 asset sizes and GitHub SHA-256
digests, twelve firmware signatures using the bundled public key, 24 binary
checksums, image checksums/chip/version identities, two fitting OTA slots per
board, matching factory apps and tested web/font assets. The ASIC-only rolling
requirement marker is present in Gamma/NerdNOS and absent from software targets.
The public latest-release endpoint and latest `update.json` advertise v1.9.15.

The [release workflow](https://github.com/bdlglua/BDLMinerV2/actions/runs/41347995159)
passed its browser/native/tool checks, signed packaging and Pages deployment.
The [main CI](https://github.com/bdlglua/BDLMinerV2/actions/runs/41347989356)
passed every board build and all tests. The follow-up
[site workflow](https://github.com/bdlglua/BDLMinerV2/actions/runs/41349250997)
also built the twelve-board 1.9.15 installer and deployed it successfully.
The short owner-run validation limits above remain unchanged.
