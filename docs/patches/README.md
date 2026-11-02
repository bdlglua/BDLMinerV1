# Statistics service V2 compatibility

`BDLMiner-stats-v2-semver.patch` applies to the separate
[`bdlglua/BDLMiner-stats`](https://github.com/bdlglua/BDLMiner-stats) repository,
based on `126ec1c`. It accepts bounded SemVer prerelease/build version labels,
including the `2.0.0-beta.1` sent by the owner-operated miners. The previous
stable-only version check rejected these reports with HTTP 400. Hardware
hashrate caps, report intervals and anonymity are preserved.

The patch passed all 19 service tests with Node 24, including parser and Worker
HTTP cases for M5 and Gamma reports. Applying it to a clean base was checked.
The workspace's Git access initially returned HTTP 403 when pushing to
`bdlglua/BDLMiner-stats`. The owner applied and published the fix from their PC
as `009fa2da830d6a5a80e56263f2dd44ad2bf5831c`. All 19 tests and the actual Cloudflare
deployment passed in [workflow 41414353158](https://github.com/bdlglua/BDLMiner-stats/actions/runs/41414353158)
on 2 November 2026, deploying Worker version `d8c4fff5-57ad-46c6-a009-35b27e34eea4`.
Current `BDLMiner-stats/main` already contains this fix. No miner firmware update
is needed for the server correction.

For an older deployment still based on `126ec1c`, from a clean clone of
**BDLMiner-stats**, with Node 22 or newer (do not reapply to current `main`):

```sh
curl -fL 'https://raw.githubusercontent.com/bdlglua/BDLMinerV2/v2/development/docs/patches/BDLMiner-stats-v2-semver.patch' -o /tmp/BDLMiner-stats-v2-semver.patch
git apply --check /tmp/BDLMiner-stats-v2-semver.patch
git apply /tmp/BDLMiner-stats-v2-semver.patch
npm test
git add src/store.js test/stats.test.js README.md
git commit -m 'fix: accept V2 prerelease statistics reports'
git push origin main
```

Pushing to that repository's `main` runs its existing **Test and deploy** workflow.
Actual publication requires its Cloudflare configuration to be present; a skipped
deployment does not establish that the live service changed. The next normal
miner report can then verify acceptance without posting synthetic public data.
