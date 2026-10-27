# Local dashboard corrections — 2026-10-27

Prepared after v1.5.2. Published release files and tags are unchanged.

## Reproduced bugs

- Repeated **Check now** commands could leave the button disabled indefinitely
  when the next statistics response was identical to the previous response.
- Check and install commands could be sent together before the next poll updated
  the controls. Both actions now share a lock and update their controls immediately.
- An available update could still have an enabled Install button when update mode
  was disabled. The dashboard now respects this mode for both actions.
- A failed command could be hidden by an older release-metadata error. Command
  errors now remain visible and the user can retry.
- Malformed inline history from a miner could mark the entire dashboard as having
  no connection. History validation now retains the previous chart and reports the
  history error separately while live statistics continue.
- Lost statistics left Start measurement enabled with a stale snapshot. A fresh
  valid statistics response is now required. Interrupted running measurements
  record missing observations instead of implying continuous samples.
- Pausing the journal did not prevent an already-running response from replacing
  its contents. Paused logs now discard that response, including late errors.
- Slow history/peer requests delayed the next local statistics poll. These requests
  now have independent schedulers, revision/caching checks and no overlapping calls
  within each scheduler.

Eight browser regressions reproduced these failures before their respective fixes.
An additional test checks that a delayed history response from an earlier boot is
discarded after the device restarts. Existing mobile/desktop layouts, settings,
uploads, backups, keyboard/touch charts and measurement exports remain covered.

## Validation

- 60 browser regressions against the actual HTML and JavaScript sources.
- 15 asset/release-tool regressions, including gzip round trips and cache changes.
- JavaScript syntax and whitespace checks.
- LilyGO T-Display firmware build, including regenerated embedded web assets and
  the existing OTA partition size limit.
- Repository CI builds and packages all ten board variants.

This change affects browser scripts only. Mining kernels, thermal controls and
saved settings are unchanged. No physical miner is attached; browser tests use
controlled API responses and do not establish real-board hashrate or temperatures.
