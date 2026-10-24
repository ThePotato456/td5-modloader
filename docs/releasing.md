# Preparing and publishing a release

Releases are prepared locally from a clean `main` branch. The preparation
script synchronizes the managed and native version strings, runs the release
gates, and creates a distributable ZIP and SHA-256 checksum. It never commits,
tags, pushes, or creates a GitHub release automatically.

## Prepare

Close any running manager built from the Release output, then run:

```powershell
./scripts/prepare-release.ps1 -Version 0.1.0
```

With GNU Make available, the equivalent command is:

```powershell
make prepare-release VERSION=0.1.0
```

Versions must use semantic versioning. A suffix such as `0.1.0-alpha.1` creates
a prerelease candidate and should be published with GitHub's prerelease flag.

The script performs these gates in order:

1. Require a clean `main` branch and a version whose Git tag does not exist.
2. Update both public loader version constants.
3. Verify formatting.
4. build and test the complete Release configuration.
5. Run native and managed static analysis.
6. Stage the manager, native loader, symbols, ten sample mods, and full pinned
   dependency license texts; omit debug-symbol files.
7. Check current documentation links/manifests and validate every staged sample
   with the native package/script validator. Callback and live acceptance are
   separate checks.
8. Include the tracked documentation wiki, Lua editor definitions and reference,
   manifest schemas, and sample source folders alongside their ready-to-import
   archives. Untracked local research is excluded.
9. Create a versioned ZIP and checksum under `out/release/v<version>`, then
   inspect the finished ZIP for required content, path safety, local machine
   paths, game/private/generated files, checksum integrity, and exact nested
   sample contents and source bytes (including packaged documentation).
10. Write the release-notes template only after those archive checks pass.

If a validation step fails, no Git commit or tag is created. The script restores
both version files to their exact pre-run contents, reads them back to verify
the cleanup, and reports that restoration. If either file cannot be restored,
the warning lists every file that still needs manual review. Fix the failure and
rerun from the clean working tree.

## Commit and push

After reviewing and testing the prepared bundle, commit the version change and
create an annotated tag:

```powershell
git add src/manager/BTD5ModLoader.Manager.Core/ProductInfo.cs `
        src/native/include/btd5loader/version.hpp
git commit -m "chore(release): prepare v0.1.0"
git tag -a v0.1.0 -m "BTD5 Mod Loader v0.1.0"
git push origin main
git push origin v0.1.0
```

Pushing a tag does not currently create a GitHub release automatically. Create
one using GitHub CLI:

```powershell
gh release create v0.1.0 `
  "out/release/v0.1.0/BTD5-Mod-Loader-v0.1.0-windows-x86.zip" `
  "out/release/v0.1.0/BTD5-Mod-Loader-v0.1.0-windows-x86.zip.sha256" `
  --title "BTD5 Mod Loader v0.1.0" `
  --notes-file "out/release/v0.1.0/RELEASE_NOTES.md"
```

Add `--prerelease` when publishing a semantic prerelease such as
`v0.1.0-alpha.1`. The same ZIP and checksum can instead be uploaded through the
GitHub Releases web interface.

Do not include game files, local profiles, installed mods, logs, saves, or Steam
account data in a release.

The complete third-party texts in `THIRD_PARTY_LICENSES` are required release
content. `THIRD_PARTY_NOTICES.md` is an index and does not replace them.
