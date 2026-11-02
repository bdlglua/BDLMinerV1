# Board SHA performance review — 2 November 2026

Starting point: `fa2f61d79c7c2e3bb684fca92d493e7810f5c4ef`, on
`v2/development`. This work follows the original beta.3 kit. That kit does not
contain these changes. No release/tag is created by this review.

## Board families and the 1 MH/s expectation

| Listed boards | Mining engine | What is established |
|---|---|---|
| M5Stack ESP32, classic LilyGO T-Display, Cheap Yellow Display, ESP32 without display | Classic ESP32 SHA plus assembly/idle-cycle software | About 1 MH/s is the target for revision 3+ at 240 MHz. The exact chip revision must be checked; model names alone do not guarantee this path. Owner-operated M5StickC Plus2 measurements support this rate. |
| M5Cardputer, M5StickS3, other M5Stack S3, T-Display-S3, ESP32-S3 without display | Resumable hardware SHA, with a software worker on the other core | Hardware SHA is supported. Lower public reports do not establish a universal chip limit or a defect. No 1 MH/s measurement is available here. |
| ESP32-S2 without display | Resumable hardware SHA, sharing its single CPU core with networking/UI | Supported; no physical performance comparison is available here. |
| ESP32-C3 without display | Resumable hardware SHA, sharing its single CPU core; 160 MHz CPU | Supported; no physical performance comparison is available here. The supported CPU clock remains 160 MHz. |
| NerdNOS | External BM1397 ASIC, controlled by its ESP32-S3 | Separate ASIC category. It requires the NerdNOS hardware; an ordinary T-Display-S3 is not a NerdNOS. No physical NerdNOS validation is recorded. |
| Bitaxe Gamma 601 | External BM1370 ASIC, controlled by its ESP32-S3 | Owner reports around 1.07 TH/s concern the ASIC, not the S3's SHA peripheral. |

On classic ESP32 revisions below 3, raw DPORT reads can corrupt the other core's
APB accesses. The firmware deliberately disables its assembly hardware loop there;
a SHA known-answer test alone cannot detect that Wi-Fi/UART failure. This guard
is preserved.

The statistics site reports live contributions and can aggregate multiple
devices in one row. A board with no current reports is not necessarily unsupported.
Power limits, firmware versions, thermal duty limiting, TLS and other work can
also change reported rates. Public entries are not a controlled board benchmark.

## Resumable SHA change

S2/S3/C3 resume SHA-256 from the first 64-byte header block's midstate, so each
nonce needs two hardware block compressions. Their old C loop copied the first
digest into RAM, then wrote it back to the peripheral as the second message.
The S2/S3 SDK additionally reads that digest through an out-of-line buffer helper.

The new candidate kernel copies the eight digest registers directly into the
eight TEXT registers, in their existing byte order, and unrolls the fixed-size
midstate/header-tail transfers. It retains the SDK register-access macros,
hardware locks, all busy waits, SHA start/continue operations, padding and exact
target/candidate semantics. It still performs the same peripheral reads/writes;
it removes intermediate RAM traffic, a helper call and transfer-loop overhead.

At startup:

1. Verify the genesis candidate and full digests for 24 further nonces, including
   carries/wrap and three different midstates. Compare with portable SHA-256d.
2. Compare three alternating pairs of 4,096-nonce scans, including lock and busy
   wait overhead. Keep the best duration per kernel. Select direct transfers only
   if their measured duration is more than 1% lower than buffered transfers.
3. If direct transfers fail correctness or do not win the comparison, retain and
   verify buffered transfers. If padding reuse fails, retry complete-block writes.
   If all correctness checks fail, the existing caller uses software mining.

The log reports `resumable SHA transfers: direct` or `buffered`, with both short
boot-scan rates in kH/s. These are isolated kernel timings, not sustained pool
throughput. Boot-test nonces never enter live mining counters or share submission.

The classic ESP32 backend, all assembly kernels, ASIC workers, task scheduling,
batch size, clocks, voltage, fan and thermal controls are unchanged. The README's
old `X GH/s`/`Y GH/s` placeholders and unsupported claim of physical NerdNOS
validation were corrected during this review.

## Verification and remaining measurements

The register-level host model executes all three production kernels against
reference SHA-256d: 16 headers, four filter limits, 19 consecutive nonces per
combination and nonce wrap. It checks complete digests, exact candidate order,
20 TEXT writes per sparse nonce, dirty padding after another SHA user, busy waits,
recursive TLS reservations and lock release. Clock fixtures exercise faster,
slower, equal-duration and unavailable-clock selection without representing real
hardware speed. Injected direct-read, nonce-specific, padding and engine failures
exercise each correctness fallback.

Local verification passed: the complete native suite, all six builds listed
below and all 61 host-tool tests, including kit integrity. Inspection of the
S2/S3 target machine code confirms that the direct kernel skips the SDK buffer
helper for ordinary nonces; candidate digest extraction still uses the SDK.
No physical performance result is inferred from the generated instructions.

Run the native regressions and builds with:

```sh
./test/host/run.sh
pio run -e esp32s3-headless -e esp32s2-headless -e esp32c3-headless \
  -e m5stack-s3 -e lilygo-t-display-s3 -e m5stickc-plus2
python3 -m unittest discover -s test/tools -v
```

The owner confirmed no ordinary S2/S3/C3 board is currently available. The cloud
workspace cannot access the owner's LAN. Simulation and successful compilation
do not establish a physical hashrate gain, stable mining, electrical readings or
1 MH/s on these chips.

When a board becomes available, record its exact model, chip revision, firmware
commit and boot log. Compare real `total_hashes` deltas over equal sustained
windows, with the same power settings, pool/protocol, display and dashboard load.
Record uptime/boot ID, health and rejected shares as well. The existing
`tools/hardware_suite.py` counter/health check can measure a configured device;
the boot log identifies which transfer kernel was selected. At least one real
board per chip family is still needed before claiming a gain for that family.
