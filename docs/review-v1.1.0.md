# Firmware review and refactor scope

The local UI has been extracted from `src/web.cpp` to maintainable `web/` sources.
The firmware still serves all pages and assets directly on the LAN; `installer/`
remains the separate USB installer published on GitHub Pages.

| Area | Review and resulting work | Validation |
|---|---|---|
| Hardware/software SHA, work and fill | Keep the accelerated kernels, per-candidate validation, fallback and self-tests. Avoid changing cycle timing to redesign the UI. | Known-answer and fill/core tests; all target builds. |
| Mining scheduling and statistics | Restore 100% as the new-configuration default; preserve user choices. Check task creation, contiguous fill counters and resource failures. | Production miner queue/startup simulations, easy-share and uptime regressions. |
| Pool/network protocol | Bound receive work, reject entire invalid/oversized lines, recover at the next newline, reject excessive merkle depth and time out handshakes. | Framing recovery, invalid-job and session protocol tests. |
| Thermal supervision and board sensors | Preserve sampling, sensor selection, thermal limiting and expiring worker permission. Expose the actual reason for reduced power. | Thermal, missing-sensor, expiry and OTA ownership regressions; target builds. |
| Settings and persistent data | Preserve staged writes, backups, validation and exact restored counters. Make performance selection explicit. | Host validation, browser submission/restore and precision tests. |
| Web, dashboard and log | Replace embedded source strings with separate compressed assets; rebuild all three pages; improve navigation, maintenance and diagnostics. | Chromium interaction, mobile layout, visibility/reconnect and gzip/cache tests. |
| GitHub/web OTA | Keep signature verification and exclusive ownership. Add board capability reporting and a USB path for boards without an OTA slot. | Production upload/updater simulations, UI tests and release signature checks. |
| Market, MQTT and notifications | Check service/resource startup and expose failures rather than silently claiming availability. Keep existing optional-service behavior. | Build coverage and runtime diagnostic handling; hardware integration still required. |
| LCD UI, WiFi setup, TLS and logging | Review dependencies and compatibility with the refactor; preserve existing interfaces and timing protections. | Build coverage and existing regressions. |

Tests simulate networking and flash and do not establish sustained 1 MH/s or safe
temperature on every board. Those measurements require physical devices, their
power supplies and cooling. No physical device was connected for this work.
