# Phase 5R manager reliability validation

Date: 2026-08-30

This record covers the automated acceptance evidence for the manager workflow
and reliability revamp. It does not close the remaining fresh-state hands-on UI
walkthrough.

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

## Remaining hands-on gate

The final unchecked Phase 5R gate is a continuous fresh-state walkthrough in
the current WPF manager: choose the ignored BTD5 test copy, install through the
single primary action, drag in a package, create and configure a profile, and
launch. This must use a disposable manager-state directory and copied game so it
does not disturb the player's normal state or registered Steam installation.
