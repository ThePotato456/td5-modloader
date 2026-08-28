# Mod package format v1

A `.btd5mod` file is a ZIP archive with `mod.json` at its root. Its manifest
must conform to `schemas/mod-manifest.schema.json` and declare identity,
semantic version, Lua entry point, loader API version, supported builds,
dependencies, ordering constraints, and requested capabilities.

## Allowed layout

Files may appear in `lua/`, `assets/`, `localization/`, `config/`, and `docs/`.
The root may contain `mod.json`, `README.md`, `CHANGELOG.md`, `LICENSE`, or
`LICENSE.md`.

Paths must use `/`, remain relative, and cannot contain empty, `.`, or `..`
components. Encrypted entries, symbolic links, duplicate case-insensitive
paths, unsupported compression, and undeclared roots are rejected.

## Package limits

- 64 MiB compressed archive and 256 MiB total uncompressed data.
- 32 MiB per file, 4,096 entries, 1 MiB `mod.json`, and 240 characters per path.

## Deterministic order

Dependencies and explicit `before`/`after` constraints form a directed graph.
Cycles, missing or incompatible dependencies, and duplicate IDs reject the
profile. Among eligible mods, profile order wins and mod ID is the final
tie-breaker.
