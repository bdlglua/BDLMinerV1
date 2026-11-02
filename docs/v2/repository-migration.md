# V2 project-name migration

Target: repository `bdlglua/BDLMinerV2`, site
<https://bdlglua.github.io/BDLMinerV2/>, firmware 2.0.1.

This change is prepared separately from published 2.0.0. Repository renaming is
an administrative GitHub operation that the available connector tools do not
expose. The owner's account has repository admin rights. The source patch does
not itself rename the remote repository or publish 2.0.1.

## Owner operation

After the prepared change passes CI, open the repository's **Settings** page,
change **Repository name** to **BDLMinerV2**, and click **Rename**. Do not create
a replacement repository with the old name. Keep signing secrets and existing
releases in the renamed repository; do not create a separate empty V2 project.

GitHub's [rename documentation](https://docs.github.com/en/repositories/creating-and-managing-repositories/renaming-a-repository)
describes redirects for repository traffic and Git operations, the exception for
Pages project URLs, and why the old name must remain unclaimed.

## Source and publication sequence

1. Verify that the new repository name resolves to the original repository ID
   `1430701879`, with its existing releases and signing configuration.
2. Check old-to-new GitHub release URL redirection. Update local remotes to
   `https://github.com/bdlglua/BDLMinerV2.git` when access is available.
3. Merge the tested naming patch, build/sign the twelve 2.0.1 profiles and verify
   their actual version, target, signatures, OTA capacity and Linux kit.
4. Publish `v2.0.1` as stable and deploy the new Pages project. Check the root and
   beta installers and update metadata against the signed release.

These are pending live operations until recorded as completed. Native redirect
fixtures test the firmware routine; they do not prove GitHub's post-rename state.
The site builder accepts the exact old repository alias only for releases through
2.0.0, then emits canonical V2 asset URLs. Other repositories and newer manifests
still require their exact canonical asset URLs; archived manifests are not edited.

## Existing devices

The stable updater in existing firmware uses GitHub release URLs, follows HTTPS
redirects and retains the same verification key. The HTTP/version compatibility
markers in new firmware also let previously distributed host tools inspect it.
Saved V1/V2 native agents normalize to `BDLMinerV2/<current version>`; custom
manual identities, stored networks and mining limits are preserved.

Older beta-channel devices use a hard-coded Pages path with no discovery
fallback. After renaming, select **stable** in those devices' update settings and
check again, or manually upload the matching `*-firmware.bin`. GitHub Pages does
not automatically preserve the old project path. New 2.0.1 firmware uses the new
path; it does not claim to rewrite older installed firmware in place.

Historical V1 release/audit descriptions retain the names actually used. Current
project links point to V2. Legacy migration predicates and binary compatibility
markers intentionally still recognize V1; they are not displayed product names.
