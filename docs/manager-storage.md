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
| `runtime/sessions/` | Per-process extracted packages; legacy `runtime/active-profile.json` is ignored |
| `storage/<mod-id>/storage.json` | Private Lua string storage, shared by that ID across profiles and versions |
| `storage/custom-towers/progress.json` | Custom tower XP keyed by stable public ID |
| `logs/` | Structured native runtime logs shown live in Options: `runtime.jsonl`, rotated to `runtime.1.jsonl` at 8 MiB (at most two files) |

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
It starts the game normally through Steam and observes the selected game
process, then writes a single-use ticket under `.btd5ml-launch` in the game
directory. The ticket binds the profile to that process's PID, exact creation
time, and installation path. It expires after 60 seconds; the runtime waits
up to 10 seconds for its matching ticket and claims it atomically. The manager
waits for acknowledgement before reporting mod loading and cleans up the ticket.

The runtime treats the ticket and profile as untrusted: it checks process
binding and the build ID, confines archives to the manager-owned package
directory, revalidates every archive, and extracts each mod into a unique
per-process session directory before creating its Lua state. Steam receives
only `-applaunch 306020`; no custom game command-line or inherited profile
environment variable is required. `BTD5ML_ACTIVE_PROFILE` and shared
`runtime/active-profile.json` are legacy handoffs and are ignored.

The current bridge invokes `on_load` inside the real game process, invokes
`on_ready` on the first rendered frame after profile loading, and advances Lua
timers once per rendered frame. Modded launch requires an explicit warning
acknowledgement; technical network enforcement is not implemented. Vanilla launch
uses Steam App ID 306020 and provides no mod authorization ticket. Without a
matching ticket the runtime creates no Lua states or gameplay hooks. Vanilla
launch still verifies the installation and blocks modified or legacy loader
files until they are repaired or removed.

Crashes may leave session data or tickets behind. A stale ticket cannot
authorize another process because creation time is part of its binding. Launch
isolation does not provide general crash recovery or save restoration.

Diagnostics exports contain build hashes, loader/profile validation, resolved
package identities, and the runtime log. They exclude game files, saves, package
contents, configuration values, and local filesystem paths.
