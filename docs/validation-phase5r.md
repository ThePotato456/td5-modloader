# Phase 5R manager reliability validation

Date: 2026-08-30

This record covers the automated and hands-on acceptance evidence for the
manager workflow and reliability revamp. All Phase 5R implementation gates
passed.

## Automated suite

The full Debug build completed with zero warnings. Both native CTest targets and
the managed manager-core integration executable passed. The managed suite uses
temporary directories and a synthetic supported game fingerprint; it does not
read or modify an installed copy of BTD5.

The suite now covers every `LoaderHealthState`:

| State | Exercised condition |
| --- | --- |
| `UnsupportedGame` | Directory without a supported executable and asset fingerprint |
| `NotInstalled` | Supported clean game with release artifacts available |
| `Healthy` | Installed files and ownership record all match |
| `Repairable` | Recorded loader file is missing and an identical source is available |
| `Conflict` | Foreign install target and separately modified manager-owned file |
| `ArtifactUnavailable` | Required matching release artifacts are absent |
| `InvalidRecord` | Damaged ownership record and separately damaged operation journal |

Install and repair tests cover preflight refusal, per-copy verification,
completed verification, cancellation/interruption reconciliation, rollback, and
safe confirmation of an operation that completed before termination. Repair
restores a missing file. It refuses to overwrite a modified file or proceed
after a conflicting preflight, and it leaves earlier missing files untouched
when the complete repair cannot succeed.

Uninstall removes verified loader-owned files, preserves a modified owned file,
reports that file as a conflict, and retains ownership state until the conflict
is resolved. Foreign install targets are classified individually and reported
as files not owned by the manager.

## Settings and migration

Manager settings round-trip the selected game copy and current profile. Startup
reconciliation now atomically clears a saved game directory that disappeared
and a saved profile that no longer exists. The warning requires an explicit new
selection; no other discovered game or profile is silently substituted.

Corrupt settings are preserved and recover to empty defaults. Existing-state
migration is idempotent, creates byte-identical backups of profile and
installation records, preserves configuration and launch history, and leaves
multiple existing profiles unselected.

## Profile and launch validation

Profile tests cover create, list, rename, duplicate, delete, enable, disable,
version switching, ordering, removal, and dependency-protected package
uninstall. Configuration tests cover valid edits, type rejection, and reset to
package defaults.

Readiness tests prove that a missing package, invalid configuration, and broken
dependency block launch. Each result includes the affected mod ID and a typed
correction action. A missing loader file routes to repair, and a vanished
profile routes to explicit profile selection. Modded launch rechecks readiness
and requires the offline-mode acknowledgement before starting a process.

## Hands-on fresh-state gate

The final gate used the current staged Debug manager and a timestamped ignored
run under `.local/manager-acceptance`. The launcher copied the existing ignored
test game while excluding loader artifacts, assigned a completely isolated
manager-state directory, and disabled automatic Steam discovery for that
process. It did not read or alter normal `%LocalAppData%\BTD5ModLoader` state or
the registered Steam installation.

The player completed the continuous WPF workflow:

1. chose the printed disposable game directory in Options;
2. installed the loader using the single context-aware primary action;
3. created and selected `New Profile`;
4. dragged `lifecycle-sample.btd5mod` from the staged samples folder;
5. enabled the mod and changed `greeting` from `Hello from Lua` to `changed`;
6. acknowledged the offline-mode requirement; and
7. launched the profile and closed the game normally.

The acceptance verifier found one installation record, one installed package,
one enabled and configured profile entry, persisted game/profile selection, and
one successful modded launch entry. The runtime log independently recorded
`sample.lifecycle:loaded`, `game_ready_frame_hook`, the changed greeting,
`Lifecycle Sample is ready`, and the deterministic timer callback.

The disposable copy retained the verified Steam Win32 4.8 proprietary hashes:

| File | SHA-256 |
| --- | --- |
| `BTD5-Win.exe` | `BDC4F4AEC679F51B8763FF7FE517A2556E392D99576045ECE117FCAFDDA27B70` |
| `Assets/BTD5.jet` | `906AA89D690C27664CE47A1A2E3EAC756D7CF551FE3E1669EC22AE814346B9A8` |

No BTD5 game process remained after the walkthrough. The isolated manager was
left open for review, and all acceptance files remain ignored and disposable.
