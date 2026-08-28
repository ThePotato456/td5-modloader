# Upstream research

The following public projects were inspected to reduce duplicated discovery
work. Their source is not vendored into this repository.

## NKHook5

- Repository: <https://github.com/NKHook/NKHook5>
- Audited revision: `6bcac69de5b76bf2bed49e5db600841bfb42ccb2`
- License: GPL-3.0
- Relevant findings: BTD5 can bootstrap through a local `wininet.dll`; the
  project contains signature scanning, game class layouts, asset overlays,
  tower factory hooks, projectile hooks, localization, and ZIP mod loading.
- Reuse rule: source may be copied or adapted under this project's
  GPL-3.0-only license. Each adapted file must carry an SPDX identifier and be
  entered in `upstream-code-provenance.md` with its exact source path, commit,
  purpose, and modification summary.

## BTD5-Decomp

- Repository: <https://github.com/NKHook/BTD5-Decomp>
- Audited revision: `b647016ea07c57d939db05555506cb88160ace9f`
- License: no license file was present at the audited revision.
- Relevant findings: the project maps high-level game classes and confirms the
  native factory/object model, but describes itself as incomplete and currently
  non-buildable.
- Reuse rule: research reference only. Do not copy, adapt, vendor, or distribute
  its code unless its copyright holders publish an applicable license.

Every game address, signature, layout, and behavior used by this loader must be
validated independently against a legally obtained local game installation.

## Redistribution rule

Distributions containing loader binaries must include the complete
corresponding loader source, the `LICENSE` file, this research record, the
provenance ledger, and all applicable third-party notices. Bloons TD 5 game
binaries and assets are never part of the corresponding loader source and must
not be redistributed with this project.
