# Manager storage and ownership

The manager keeps its mutable state under `%LocalAppData%\BTD5ModLoader`. Mod
packages are copied to `packages/<mod-id>/<version>/package.btd5mod`; profiles,
installation records, logs, and future backups use separate subdirectories.
Nothing in this state directory belongs in the source repository.

## Loader installation safety

The manager accepts only a known 32-bit BTD5 executable and matching executable
and asset hashes. It installs the proxy, runtime, and symbol maps without changing
`BTD5-Win.exe` or `Assets/BTD5.jet`. Any existing target file is a conflict and is
left untouched.

An installation record stores the relative path and SHA-256 of every file the
manager created. Verification compares those hashes. Repair restores only a
missing recorded file from an identical release artifact, and never replaces a
modified file. Uninstall deletes only recorded files whose current hashes still
match; modified files are preserved and reported for manual recovery.

## Package intake

The file picker and drag-and-drop path run the same validation used by automated
tests. The manager shows package identity, version, dependencies, requested
capabilities, build compatibility, and validation errors before installation.
Archive traversal, symbolic links, duplicate paths, invalid layouts, excessive
sizes, malformed manifests, and unsupported loader APIs or builds are rejected.

## Runtime handoff and launch

Before modded launch, the manager validates the selected game, loader
installation, profile, package versions, dependencies, and deterministic order.
It writes an atomic active-profile handoff under `runtime/active-profile.json`
and passes that location to the copied game process. The native runtime treats
the handoff as untrusted: it checks the build ID, confines archives to the
manager-owned package directory, revalidates every archive, and extracts each
mod into a unique per-process session directory before creating its Lua state.

The current bridge invokes `on_load` inside the real game process. It does not
invoke `on_ready` yet because that callback requires the verified game-ready
hook planned for Phase 6. Modded launch requires an explicit warning
acknowledgement until Phase 9 network enforcement is complete. Vanilla launch
uses Steam App ID 306020 and does not pass a mod profile.

Diagnostics exports contain build hashes, loader/profile validation, resolved
package identities, and the runtime log. They exclude game files, saves, package
contents, configuration values, and local filesystem paths.
