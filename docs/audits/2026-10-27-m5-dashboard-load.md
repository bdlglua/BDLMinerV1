# M5StickC Plus2: dashboard load and hashrate

## Report and scope

The miner's local display was around 1 MH/s while FindMyBlock showed about
800 kH/s. The reported trigger was opening the local dashboard. No physical
M5StickC Plus2 or user-device logs were available during this review; the exact
hardware slowdown and the size of any improvement have not been measured.

## Changes shipped in v1.6.3

- Refresh dashboard statistics every ten seconds instead of three: about 70% fewer
  recurring local statistics requests when responses are fast. The first request
  remains immediate. Hidden-tab suspension and non-overlapping polling remain.
- Send each HTML page with an exact Content-Length and four payload writes,
  instead of many separately framed HTTP chunks. Keep the page bodies in flash;
  only the small dynamic head is assembled in RAM. Set the document language
  correctly before the page scripts load.
- Reserve the statistics JSON response buffer before serialization, avoiding
  repeated String-buffer growth as individual fields are appended.
- Run the HTTP worker at priority 1 on core 0, sharing that core with its
  software miner. It previously ran at priority 2 and could preempt that miner
  throughout a response. Hardware mining remains at priority 19 on core 1;
  pool networking and the thermal/supervision loop keep their higher priorities.

The SHA-256d kernels, counted nonces, hardware timing, CPU frequency, temperature
limits and supervision lease are unchanged. A short initial Wi-Fi transfer can
still consume resources; this change does not promise zero impact or a measured
1 MH/s on a connected device.

## Verification

The production page-delivery function is executed against an HTTP recorder for
all three pages in English and French: exact byte length, four writes, unchanged
HTML bodies, language and versioned asset links are checked. A browser regression
uses unaccelerated polling timers and the Playwright clock to verify one immediate
statistics request followed by the next after ten seconds, with no repeated
history download. Existing browser, protocol, persistence, upload and thermal
checks remain part of CI; the M5StickC Plus2 and Gamma firmware are compiled.

A separate diagnostic outside the tracked test suite exercised the production
M5 miner statistics for two simulated hours at 1 MH/s across hardware/software/fill
counters, including 32-bit rollover. Exact totals and rate decay after slowdown
and stopping were verified. This validates accounting logic, not the physical
SHA peripheral's throughput.

For a real comparison, use the same power, cooling and pool settings; compare
counter-derived hashes over equal-length dashboard-closed/open intervals and
inspect hardware/software/fill engine rates, temperature, effective power and
accepted/rejected shares. FindMyBlock's [FAQ](https://findmyblock.xyz/faq) explains
that its current rate is estimated from accepted shares and recommends its 24-hour
average for comparison. A persistent loss should be checked against both local
counter measurements and accepted-share statistics.

## Released and test firmware

The signed [v1.6.3 release](https://github.com/bdlglua/BDLMinerV1/releases/tag/v1.6.3)
includes these changes for all supported variants. Existing M5StickC Plus2 devices
can install the matching signed firmware through GitHub update discovery or the
local update page. Reload the dashboard afterwards. v1.6.2 does not contain these
changes. Real-board performance validation remains outstanding.

The `m5stickc-plus2-dashboard-test-<commit>` artifact from a successful CI run
contains unsigned development images and this note. It is not a new signed
GitHub release. For an existing BDLMiner installation, the matching
`m5stickc-plus2-firmware.bin` can be installed through the password-protected local
firmware-update form. The factory image is for a USB install and erases settings.
Reload the dashboard after updating.
