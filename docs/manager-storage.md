# Manager storage and ownership

The manager keeps its mutable state under `%LocalAppData%\BTD5ModLoader`. Mod
packages installed through drag-and-drop are copied to
`packages/<mod-id>/<version>/package.btd5mod`. A user can also copy a valid
`.btd5mod` file directly into the `packages` folder (or one of its subfolders);
the manager discovers it when its window regains focus. Profiles, installation
records, logs, migration backups, and runtime handoffs use separate
subdirectories. Nothing in this state directory belongs in the source
repository.

## State layout

| Path | Purpose |
| --- | --- |
| `manager.json` | Atomically saved game-directory and current-profile selection |
| `packages/` | Installed and directly copied `.btd5mod` archives |
| `profiles/` | Named profiles, per-profile configuration, ordering, and launch history |
| `installations/` | Loader-owned file manifests keyed by game directory |
| `operations/` | Crash-recovery journals for an in-progress install or repair |
| `backups/revamped-manager-v1/` | Byte-for-byte pre-migration profile and installation records |
| `migrations/` | Completed one-time state migration markers |
| `runtime/` | Atomic active-profile handoff for the next modded launch |
| `logs/` | Structured native runtime logs shown live in Options |

The first revamped-manager startup validates compatible existing state, backs up
profiles and installation records without rewriting them, and infers selections
only when there is exactly one choice. Multiple profiles or installations remain
unselected. A completed marker makes the migration idempotent.

## Loader installation safety

The manager accepts only a known 32-bit BTD5 executable and matching executable
and asset hashes. It installs the proxy, runtime, and symbol maps without changing
`BTD5-Win.exe` or `Assets/BTD5.jet`. Any existing target file is a conflict and is
left untouched.

An installation record stores the relative path and SHA-256 of every file the
manager created. Verification compares those hashes. Install and repair use a
crash-safe operation journal, preflight the complete operation, verify each
copy, and verify the final result. Repair restores only a missing recorded file
from an identical release artifact and never replaces a modified file.
Uninstall deletes only recorded files whose current hashes still match;
modified files are preserved and reported for manual recovery.

## Package intake

The drag-and-drop path and package-folder discovery run the same validation used
by automated tests. A dropped valid package installs immediately. The manager
shows package identity, version, dependencies, requested capabilities, build
compatibility, and validation errors.
Archive traversal, symbolic links, duplicate paths, invalid layouts, excessive
sizes, malformed manifests, and unsupported loader APIs or builds are rejected.

Package installation and profile activation are separate. A package can remain
installed without belonging to any profile. Configuration, enabled state,
selected version, and order belong to each profile; uninstall is blocked while
a profile still references that package version.

## Runtime handoff and launch

Before modded launch, the manager validates the selected game, loader
installation, profile, package versions, dependencies, and deterministic order.
It writes an atomic active-profile handoff under `runtime/active-profile.json`
and passes that location to the copied game process. The native runtime treats
the handoff as untrusted: it checks the build ID, confines archives to the
manager-owned package directory, revalidates every archive, and extracts each
mod into a unique per-process session directory before creating its Lua state.

The current bridge invokes `on_load` inside the real game process, invokes
`on_ready` on the first rendered frame after profile loading, and advances Lua
timers once per rendered frame. Modded launch requires an explicit warning
acknowledgement until Phase 9 network enforcement is complete. Vanilla launch
uses Steam App ID 306020 and does not pass a mod profile.

Diagnostics exports contain build hashes, loader/profile validation, resolved
package identities, and the runtime log. They exclude game files, saves, package
contents, configuration values, and local filesystem paths.
