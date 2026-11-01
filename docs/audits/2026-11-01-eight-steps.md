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
or promise a pool payout. Existing public-data TLS transport is retained.

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
