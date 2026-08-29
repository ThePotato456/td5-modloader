# Manager revamp specification

This document defines the Phase 5R manager workflow. It replaces the original tab-based manager
design while preserving existing profiles, packages, installation records, configuration, and
launch history.

## Design goals

- Keep the current profile visible at all times and persist it across manager restarts.
- Present package installation, profile membership, enabled state, selected version, and
  configuration as separate states.
- Give routine actions plain names. “Remove from profile” never means “uninstall package.”
- Inspect the loader automatically and offer one primary action appropriate to its current state.
- Preserve every foreign or modified game-directory file unless the user resolves it manually.
- Keep diagnostics and destructive recovery actions available without making them part of the
  everyday launch path.

## Window structure

The player-facing window follows a compact three-pane layout:

1. **Mods** lists installed package versions and shows whether each package is absent, disabled, or
   enabled in the current profile. Installing a package adds it to the library; it does not silently
   alter a profile.
2. **Profiles** makes the current profile explicit and supports create, select, rename, duplicate,
   and delete. A missing saved profile produces no selection rather than choosing another profile.
3. **Details** explains the selected mod's package and current-profile state and provides contextual
   configuration information. Add, enable/disable, configure, and uninstall actions appear here only
   when they apply to the selected package.

Loader health and launch actions remain visible in a fixed bottom bar. The visual design uses the
dark, high-contrast, three-pane direction selected for the project without bundling game artwork.

## Persisted manager selection

`%LocalAppData%\BTD5ModLoader\manager.json` stores the selected game directory and current profile.
The file is written atomically. An invalid or unsupported settings file is preserved, both selections
are cleared, and the manager reports the recovery instead of guessing.

## Loader health states

| State | Meaning | Primary action |
| --- | --- | --- |
| `UnsupportedGame` | The selected directory is not a supported BTD5 build. | Recheck after choosing a game |
| `NotInstalled` | No ownership record or conflicting loader files exist. | Install loader |
| `Healthy` | Every recorded loader-owned file matches its hash. | Recheck |
| `Repairable` | One or more owned files are missing and matching release files exist. | Repair loader |
| `Conflict` | A foreign target or modified owned file exists. | Explain manual recovery; do not overwrite |
| `ArtifactUnavailable` | Required matching release files are unavailable. | Restore/reinstall the manager release |
| `InvalidRecord` | The ownership record is corrupt or unsupported. | Preserve it and explain recovery |

Normal releases locate loader artifacts beside the manager. Developers can set
`BTD5ML_ARTIFACT_DIRECTORY` to test a separate staged artifact directory; this override is not shown
in the player UI.

Install writes the ownership record only as part of the transaction and verifies the result. Repair
verifies after restoring missing files. A partial uninstall rewrites the record to contain only the
modified files it deliberately preserved, so already removed files are not later mistaken for a
repairable installation.

## Mod configuration

Configuration belongs to a mod entry in one profile. The editor infers controls from the package's
`configuration_defaults`:

- JSON booleans use a checkbox.
- JSON strings use a text field.
- JSON numbers use a finite-number field with invariant parsing.

Unknown keys, missing keys, and values whose JSON type differs from the default are rejected by the
core service, not only by the window. Reset restores the package defaults. Richer metadata such as
descriptions, ranges, and choices remains a future package-schema extension.

## Remaining Phase 5R work

- Add a version selector when multiple versions of one mod are installed.
- Add dependency-aware change previews so enabling a mod can propose required dependencies.
- Add richer configuration metadata for descriptions, ranges, and choices.
- Add an operation journal and interruption/recovery tests for install and repair.
- Complete hands-on UI and fresh-account acceptance testing.
