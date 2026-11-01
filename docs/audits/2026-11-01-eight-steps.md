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
