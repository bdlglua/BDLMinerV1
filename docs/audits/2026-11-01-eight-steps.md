# Candidate durability and release validation — 1 November 2026

Starting point: `a696e7bfc26cdcc00d6a379b4bb0af9dba7a2773`.
Work follows the requested eight-point order. Hardware checks are reported
separately from offline simulations and are not assumed from successful builds.

## 1. Durable candidate metadata

The last candidate's hash, height and pool submission state are now saved in
two independent NVS extension slots (`blockA`/`blockB`) with CRC, version magic,
verified writes and wrap-safe sequence selection. Original `statA`/`statB`
layouts are unchanged so old firmware/OTA rollback can still read totals.
Only changed candidate metadata is written; failed writes remain dirty and retry.

On reboot a queued proof becomes stale, and a submitted proof without a saved
answer becomes unanswered. Saved metadata is displayed as belonging to a
previous session; it is neither resubmitted nor announced as a new discovery.
It does not establish Bitcoin chain inclusion or a payout. Ordinary statistics
backups retain their existing format; candidate evidence is device-local NVS.

Fault injection covers failed writes and retry, reboot, corruption/truncation
of either slot, fallback to the preceding proof, unchanged lifetime-stat layout,
no replay and no repeated discovery alert. Browser coverage checks the restored
record while the session candidate counter is zero.

## 2. Explorer observations, independent from pool ACKs

The background market task checks the latest candidate hash through mempool.space's
Esplora `/block/:hash/status` endpoint, then verifies the reported height against
the work and reads `/blocks/tip/height` for the confirmation count. An explorer
report is identified as such; the firmware does not verify a full Bitcoin chain
or promise a pool payout. Explorer checks authenticate the server certificate
and hostname with bundled public ISRG X1, GTS R4 and DigiCert Global G2 roots,
using the existing mining/watchdog/heap safeguards. A certificate/time/trust
failure remains an unknown observation; there is no insecure fallback.

There is no request without a local/saved candidate. New hashes wait 15 seconds
for pool submission. Successful observations refresh every two minutes (ten
minutes after six confirmations); failures back off from 30 seconds to six hours.
Responses are decoded into a bounded 512-byte sink. Missing/malformed/oversized
responses and HTTP failures leave current chain acceptance unknown in the API;
a 404 means not indexed, not rejected. Reorganizations can reverse an observation.
Results for a replaced candidate never attach to the new proof. After reboot,
observations are checked afresh rather than restoring stale chain assertions.

Tests replace HTTP/TLS/notifications locally and cover confirmations, reorgs,
height mismatches, invalid tip values, HTTP errors, bounded/chunked/truncated
bodies, changed work during requests, low memory, WiFi loss and timer wrap.
Browser cases cover source attribution, counts, uncertainty on errors and French.

## 3. Complete loopback Stratum block test

The integration test runs a real loopback TCP server and the production V1
session, miner job builder, ASIC SHA-256d verification, priority queue, share ACK
correlation and NVS save/load. It reconstructs the genesis coinbase through
Stratum's extranonce fields, then supplies the known ASIC nonce. The Python
server independently rebuilds and hashes the submitted header with hashlib.
Only `127.0.0.1` is contacted; no block or share reaches a public pool.

Acceptance, explicit rejection, disconnection before ACK and a clean job before
the ACK all pass. The last case delivers a stale ASIC result and asserts it is
ignored. A full ordinary-share queue cannot displace the candidate, duplicate
ASIC results do not double-submit, and the observed pool outcome survives reboot
without replay. Both TCP reads and writes are deliberately fragmented.

## 4–6. Physical checks and local measurement preparation

Read-only attempts to the two supplied private device endpoints both returned
HTTP 403 `Domain forbidden`. The selected workspace reports no VPN configuration
or private TCP grant. No direct cloud hardware measurement, actual OTA or
bootloader rollback has therefore been claimed. The owner reports a
failed power strip caused the earlier interruption, and the supplies were checked.

The owner subsequently supplied a LAN snapshot recorded at 17:41:09 UTC, with
both boards on 1.9.13: M5StickC Plus2 at 1.002 MH/s and 53 C (IMU), Gamma 601 at
1.058 TH/s, 525 MHz, ASIC 51 C and regulator 57.25 C. The Gamma reported an
automatic 70% fan at 8,294 RPM and 356 accepted / 1 rejected share. The M5
reported zero shares and a disabled configured thermal limit. The helper now
retains the assigned pool difficulty so zero shares can be interpreted, and
records power/thermal conditions and share deltas during the comparison. The
default 65 C limit is recommended before both phases; settings must remain
unchanged between phases. These point-in-time user readings do not validate the
draft 1.9.14, optical rendering, fan response, dashboard load or OTA rollback.

The LAN-only procedure and portable read-only tool are in
[hardware validation](../hardware/validation.md). They record actual device
statistics, compare dashboard conditions using hash/uptime deltas with equal
sampling, and observe OTA version/trial transitions. The tool never writes
settings, flashes or restarts devices; a real rollback requires controlled
physical/serial reset during the unconfirmed trial. Unit cases reject counter
resets and OTA results without the required observed trial/previous version.

## 7. Candidate release preparation

The next development version is 1.9.14, with English/French notes. A dedicated
`release/prepare-1.9.14` branch uses the signing workflow to create a GitHub draft
and archive the signed files plus installer preview. Draft preparation does not
deploy Pages or enter the public automatic-update channel. Publishing the draft
later triggers the site workflow. The README continues to identify the latest
published version accurately. Physical checks above remain explicitly pending.

The signed draft at commit `152dec22106c0809a239698c9ee27c349b35debc` contains
51 assets. Offline verification passed all twelve firmware signatures, binary
checksums and image/layout checks, as well as the installer preview. The public
automatic-update channel remains on 1.9.13.

## 8. Public Pool listing preparation

The owner's existing `public-pool-ui` fork adds five HTML lines linking the
normalized `BDLMiner` device name to its repository. The actual backend parser
maps the native `BDLMinerV1/<version>` identity to that name. Dependency install
and the Angular production build pass; existing Moment/CSS warnings remain.

Creating the upstream draft pull request returned HTTP 403 `Resource not
accessible by integration`. No pull request was created. The prepared comparison
is [available on GitHub](https://github.com/benjamin-wilson/public-pool-ui/compare/master...bdlglua:master?expand=1).
The upstream maintainers still decide acceptance and the open-hardware marker.
