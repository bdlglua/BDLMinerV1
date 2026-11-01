# V2 beta readiness — 2 November 2026 (Europe/Paris)

Scope: prepare a first testable beta, redesign the miner's own local dashboard
and optimize mining work on `v2/development`. The published stable release remains
1.9.15. This record does not establish real V2 hardware acceptance.

## Mining changes

Generic double SHA-256 formerly serialized its first digest, invoked the general
SHA padding path again and reloaded those digest bytes. The second digest now
uses the eight existing SHA words, fixed 256-bit padding, an unrolled schedule and
constant initial state. Its schedule is kept out of the first hash's padding stack.

NerdNOS formerly hashed a returned header in its driver and then hashed it again
in the common submission adapter. The shared adapter now returns the independently
verified difficulty and enforces the existing hardware threshold before recording
a result. The driver reuses that result for error accounting and credited work.
Duplicates, stale generations, unnegotiated bits and below-filter results retain
their rejection paths. Gamma uses the same adapter with its existing threshold.

The source from `sha::prepare()` through `sha::scan()` is byte-for-byte unchanged
relative to 3eb9b5a. Hardware/software assembly scan loops, voltage, frequencies,
thermal/fan policy and hashrate-counter accounting are unchanged.

Independent `hashlib` checks cover 517 input lengths, including null/empty input,
unaligned memory, in-place output, SHA padding boundaries and multi-block data.
The native SHA scan, ASIC adapter, NerdNOS, block pipeline and real V1/V2 protocol
fixtures are exercised by `./test/host/run.sh`.

## Benchmark scope

[Raw native measurements](../benchmarks/v2-native-sha256d-2026-11-02.json) record
the old commit, both SHA source hashes, compiler/flags, architecture, seven
alternating samples and median rates for 64/80-byte inputs. One warmup round is
excluded. Reproduce with:

```bash
python3 tools/benchmark_sha256d.py --baseline 3eb9b5a --iterations 750000 --runs 7
```

These are portable SHA-256d measurements on the host computer. They do not imply
an equivalent increase in M5 hardware SHA MH/s or ASIC TH/s, and they do not
measure cooling or electrical efficiency. Board-level counter measurements are
still required to establish any actual throughput/temperature benefit.

## Dashboard and test kit

The embedded page prioritizes current work and clearly separates a running ASIC,
cooling/pause and pool connection. Diagnostics expand on demand. Graph controls,
export, accessibility and EN/FR labels retain their regression coverage.

The host hub uses the configured power limit in its control and displays effective
power separately. Poll ordering prevents old reads/errors from replacing a newer
applied setting. Browser tests cover unsaved power edits, delayed chart/fleet
responses and lost/recovered connections.

The offline Linux kit validates its M5/Gamma firmware hashes, chip, target/version,
factory application and both OTA capacities. It contains public verification
material, explicit scripts/tools, checksums and EN/FR instructions. No private keys,
runtime database or local reports are copied. Archive creation checks the manifest
again and excludes newly created caches. Input corruption does not replace a
previous valid kit.

`check-miners.sh` only reads statistics. A V1 board, pending firmware trial,
stopped/paused miner, disconnected pool or failed health check cannot receive a
V2-ready result. No script automatically installs a firmware or changes frequency.
The CI's `v2-linux-test-kit` artifact contains unsigned development images.

Real M5/Gamma OTA, counter-rate, dashboard load and visual acceptance remain
pending on the owner's LAN. NerdNOS and other physical boards also remain
unvalidated for V2. Loopback fixtures are software tests, not hardware evidence.
