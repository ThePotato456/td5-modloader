# BTD5 Mod Loader

An in-development, offline-focused mod loader for the 32-bit Windows Steam
release of Bloons TD 5. Low-level integration is implemented in C++ and the
public mod API uses sandboxed Lua 5.4.

Development progress and mandatory phase gates are tracked in
[`ACTIONPLAN.md`](ACTIONPLAN.md). Build instructions are in
[`docs/building.md`](docs/building.md).

## Licensing and game files

The loader source is licensed under GNU GPL version 3 only; see [`LICENSE`](LICENSE).
Some future portions may be adapted from GPL-licensed NKHook5 and will be
identified in [`docs/upstream-code-provenance.md`](docs/upstream-code-provenance.md).

Bloons TD 5, its binaries, and its assets are not included and must not be
committed or redistributed. Users must supply their own legally obtained game
installation for integration testing.
