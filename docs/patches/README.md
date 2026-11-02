# Statistics service V2 compatibility

`BDLMiner-stats-v2-semver.patch` applies to the separate
[`bdlglua/BDLMiner-stats`](https://github.com/bdlglua/BDLMiner-stats) repository,
based on `126ec1c`. It accepts bounded SemVer prerelease/build version labels,
including the `2.0.0-beta.1` sent by the owner-operated miners. The previous
stable-only version check rejected these reports with HTTP 400. Hardware
hashrate caps, report intervals and anonymity are preserved.

The patch passed all 19 service tests with Node 24, including parser and Worker
HTTP cases for M5 and Gamma reports. Applying it to a clean base was checked.
It is not a miner firmware update and has not been deployed: the workspace's Git
access returned HTTP 403 when pushing to `bdlglua/BDLMiner-stats`.

From a clean clone of **BDLMiner-stats**, with Node 22 or newer:

```sh
curl -fL 'https://raw.githubusercontent.com/bdlglua/BDLMinerV1/v2/development/docs/patches/BDLMiner-stats-v2-semver.patch' -o /tmp/BDLMiner-stats-v2-semver.patch
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
