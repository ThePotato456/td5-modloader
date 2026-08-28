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
