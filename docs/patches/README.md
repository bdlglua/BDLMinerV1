# Patches for related repositories

## Account-root website

`bdlglua.github.io-root-site.patch` records the initial migration of the separate
[`bdlglua/bdlglua.github.io`](https://github.com/bdlglua/bdlglua.github.io)
repository from `f3561c7bbbc78b77015d27fd1f2f3a58726070b0`.
It preserves the original BDLXS / NovaFlow page and its assets under `/bdlxs/`,
then publishes the complete BDLMiner website and USB installer at
<https://bdlglua.github.io/> using GitHub Pages Actions.

The workflow reuses BDLMinerV2's existing channel builder and published factory
images. Stable stays the default, `/beta/` remains opt-in, and the original
project Pages site at `/BDLMinerV2/` remains available for existing links and
firmware updates. The root site refreshes hourly or on a manual workflow run.

The patch was applied to a clean base and its resulting tree checked against
the prepared migration. Local browser checks covered both installers, all
24 board selections/manifests, image paths, mobile layout and BDLXS assets.
All four original BDLXS files were preserved byte-for-byte.

**Already applied; do not reapply to current `main`.** Write access became
available, and the migration was published as `7810738` on 2 November 2026.
The initial attempt to change the Pages settings was refused. Commit `1f7ccf6`
instead uses the existing Pages deployment permissions directly. The complete
site was deployed and its live root, beta, USB manifests and BDLXS page passed
verification in [workflow 41492450646](https://github.com/bdlglua/bdlglua.github.io/actions/runs/41492450646).

The current workflow also runs after every push and waits for any automatic
branch-based Pages build before publishing the generated site. See the
[current publishing workflow](https://github.com/bdlglua/bdlglua.github.io/blob/main/.github/workflows/pages.yml)
for subsequent fixes; the archived patch retains the initial migration only.

## Statistics service V2 compatibility

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
