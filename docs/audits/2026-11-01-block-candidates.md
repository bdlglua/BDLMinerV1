# Block candidate review — 1 November 2026

Unreleased. Starting point: `16c6fa385c943274fe13207ee95dc9c33d8e1ebe`.

Reviewed local proof checks, queued submissions, V1/V2 replies, saved totals,
physical display alerts and the local dashboard. No live share or notification
is sent by these tests, and no physical mining device is required.

| Fault | Correction | Regression coverage |
|---|---|---|
| A network-target proof was counted and announced even if the ordinary 16-share queue was full and discarded it. | Reserve two candidate slots, consume these before ordinary shares, and explicitly report/log exceptional candidate queue exhaustion. | The known genesis proof is delivered ahead of 16 ordinary shares; two queued easy-header proofs survive a third-candidate overflow. |
| A late software batch from invalid work could increment the block counter after a clean job or disconnect. Repeated ASIC results could increment it again. | Serialize candidate registration with job invalidation; deduplicate against the last four candidate hashes. | Late callbacks across clean jobs/disconnects, duplicate proofs and reused submission IDs. |
| The alert displayed the current job height, possibly different from the candidate's work. The first watch tick swallowed an earlier candidate. | Retain the candidate's own height/hash; start the session alert counter at zero. | Current height 200 with candidate height 100; candidate present before the first alert tick, repeat calls and optional beep. |
| A save requested between finding a candidate and the next miner tick omitted that candidate from lifetime blocks. | Update the lifetime count with the candidate registration. | Snapshot before ticking includes the new candidate. |
| Screens/notifications/MQTT called local candidates confirmed-sounding “blocks found”, without recording submission/ACK state. | Use candidate wording, expose proof and submission state, correlate replies by submission ID independently of the ordinary share log. A pool ACK remains a share ACK. | Accepted/rejected replies, ordinary-log eviction, disconnection, ID reuse, mobile hash wrapping, French text and escaped API content. |
| Compact network targets ignored the sign bit and silently truncated overflowing values. | Reject negative, zero and overflowing compact targets in both protocols; preserve the last valid V1 job. | Compact decoding boundaries, malformed V1 notifications and SV2 previous-hash frames. |

Existing statistics backup/NVS layouts and JSON counter names are preserved.
The latest candidate details are session data; lifetime candidate counts persist.
The firmware has no chain inclusion or payout verification mechanism, so
`block_candidate.chain_confirmed` is deliberately null. Pool ACKs, including
batched SV2 ACKs, do not establish Bitcoin chain inclusion or a reward.

Hashing kernels, CPU/ASIC frequencies and thermal controls are unchanged.

Validation: the full offline host suite passes, including software and ASIC
candidate paths, both Stratum protocols and the first-tick alert; 95 browser
regressions and 16 tooling tests pass. Physical-display render checks pass at
seven sizes in English/French. Local firmware builds pass for M5StickC Plus2,
Bitaxe Gamma 601, NerdNOS and ESP32-C3. These checks do not establish actual
Bitcoin chain acceptance, a payout or physical hardware behavior.
