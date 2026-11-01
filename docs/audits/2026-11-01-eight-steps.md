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
or private TCP grant. No direct cloud hardware measurement or remote OTA
operation has therefore been claimed. The owner reports a
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

At 18:20:29 UTC the owner supplied a completed closed/open comparison under
1.9.13. Recomputed counter rates are 1.007335 / 1.001669 MH/s for the M5 (-0.563%)
and 1.074122 / 1.075200 TH/s for the Gamma (+0.100%). Maximum temperatures were
unchanged at 54 C (M5 IMU) and 52 C (Gamma ASIC). Eighteen samples per board per
phase showed pool connections and full effective power, unchanged boot IDs and
no health issues. The Gamma accepted 15 and 17 shares respectively with no new
rejects, retaining its 525 MHz profile and automatic 73% fan. These approximately
three-minute phases show no large dashboard penalty, not long-term validation
or results for the unpublished 1.9.14 firmware.

The M5 was assigned V2 difficulty 10,000, implying roughly 497 days per pool
share on average at 1 MH/s. This explains why zero shares in this run do not
indicate a broken submission path. The thermal limit remained disabled in both
phases; restoring the default 65 C limit is still recommended.

At 18:32:08 UTC the owner supplied the M5 OTA report: confirmed 1.9.13, a new
boot on pending 1.9.14, then that same 1.9.14 boot confirmed. The final readings
are approximately 1.005 MH/s, 54 C (IMU), full effective power and Stratum V2
connected. The API's 175 seconds of mining-session uptime is consistent with
the separate three-minute board-boot OTA timer. Transient request errors were
reported, without an additional new-firmware boot change; their exact causes
cannot be established from this report. This validates the owner-observed M5
OTA confirmation sequence, not rollback or long-duration stability.

Observer version 3 prints a pending-trial cue for the physical reset,
groups repeated observations with UTC times and counts, and refuses to count a
previous version still in trial as a successful rollback. Simulated observer
tests cover confirmation and rollback after hundreds of baseline polls and
temporary connection failures; these are separate from the owner's LAN evidence.

At 18:41:20 UTC the owner supplied the controlled Gamma rollback report. It
records confirmed 1.9.13, a new pending 1.9.14 boot observed six times, then a
third boot back on confirmed 1.9.13. The result is `rollback`, with the pending
trial observed and `passed: true`. This establishes the supplied bootloader
rollback sequence after the requested physical reset; crash/timeout rollback
and physical M5 rollback are not established by this test.

The last sample arrives at zero seconds of mining-session uptime: SV2 awaits
time synchronization, the ASIC awaits pool work, startup fan duty is forced to
100%, and the temperature is the 29 C board sensor. The regulator's `0x0840`
status has OFF set while the ASIC is unpowered and none of the unconditional
fault bits. The saved 525 MHz profile and automatic fan setting remain present.
That immediate startup sample did not establish resumed hashing or fan rotation.

At 18:56:46 UTC the owner supplied the Gamma OTA confirmation report: restored
1.9.13, a new 1.9.14 boot observed pending 74 times, then confirmation on the
same boot. The final sample reports a running BM1370 at 525 MHz, approximately
1.066315 TH/s, ASIC 54 C, regulator 58.75 C, automatic fan 79% at 8,667 RPM,
regulator status 0 and hardware errors 0.079968%. Stratum V2 is connected with
14 accepted / 0 rejected shares and none pending. Full effective power, the
65 C limit and the saved automatic fan/profile settings are retained. This
establishes the owner-observed Gamma confirmation and resumed mining after the
controlled rollback/reinstallation sequence. M5 confirmation, Gamma controlled
rollback and Gamma confirmation are now documented separately. Physical M5
rollback, crash/timeout-triggered rollback and prolonged stability are untested.
At that point the signed release was still a draft; publication is recorded
below. The English/French source notes include the completed owner-run checks
and their remaining limits.
The owner subsequently confirmed both physical screens are readable, answering
the legibility/overlap/clipping question for the M5 and Gamma on 1.9.14. This is
an owner observation, not a cloud optical measurement or coverage of every fault
screen. Controlled fan response remains outstanding; no NerdNOS device was
available for this review.

## 7. Candidate release preparation

The next development version is 1.9.14, with English/French notes. A dedicated
`release/prepare-1.9.14` branch uses the signing workflow to create a GitHub draft
and archive the signed files plus installer preview. Draft preparation does not
deploy Pages or enter the public automatic-update channel. Publishing the draft
later triggers the site workflow. The README continues to identify the latest
published version accurately. Uncompleted physical checks above remain pending.

The signed draft at commit `152dec22106c0809a239698c9ee27c349b35debc` contains
51 assets. Offline verification passed all twelve firmware signatures, binary
checksums and image/layout checks, as well as the installer preview. The public
automatic-update channel remained on 1.9.13 while this draft was being reviewed.

### Publication completed

Following the owner's explicit publication request, annotated tag `v1.9.14`
was pushed at commit `00707a58ab2ae3988b775c21ae8c51ebb8df2655`. The firmware and
build inputs match the owner-tested draft; subsequent changes were confined to
documentation and the host-only LAN observer/tests. The normal tag workflow
rebuilt and signed all twelve variants, updated the existing draft's notes and
files, then published it on 1 November 2026 at 20:22:33 Europe/Paris.

[Release v1.9.14](https://github.com/bdlglua/BDLMinerV1/releases/tag/v1.9.14)
contains 51 assets. Independent downloads verified all 51 GitHub asset digests
and sizes, twelve firmware signatures with the bundled public key, 24 binary
checksums, image checksums/chip IDs, both OTA slots, factory app identity and
the tested web/font assets. The public latest-release endpoint and latest
`update.json` both advertise v1.9.14. All twelve installer layouts were checked.

The [release workflow](https://github.com/bdlglua/BDLMinerV1/actions/runs/41340099997)
passed web tests, native/tool tests, signed packaging and Pages deployment.
The [main CI](https://github.com/bdlglua/BDLMinerV1/actions/runs/41340086093)
also passed every board build plus native/tool and browser checks. The
[web flasher](https://bdlglua.github.io/BDLMinerV1/) was deployed with 1.9.14.
The hardware-validation limits above remain explicit; publication does not
turn the short owner-run checks into long-duration or all-board validation.

## 8. Public Pool listing preparation

The owner's existing `public-pool-ui` fork adds five HTML lines linking the
normalized `BDLMiner` device name to its repository. The actual backend parser
maps the native `BDLMinerV1/<version>` identity to that name. Dependency install
and the Angular production build pass; existing Moment/CSS warnings remain.

Creating the upstream draft pull request returned HTTP 403 `Resource not
accessible by integration`. No pull request was created. The prepared comparison
is [available on GitHub](https://github.com/benjamin-wilson/public-pool-ui/compare/master...bdlglua:master?expand=1).
The upstream maintainers still decide acceptance and the open-hardware marker.
