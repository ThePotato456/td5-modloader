# Mod package format v1

[Documentation home](README.md) · [First mod tutorial](mod-authoring.md) ·
[Manifest JSON schema](../schemas/mod-manifest.schema.json)

A `.btd5mod` file is a ZIP archive with `mod.json` at its root. Its manifest
must conform to `schemas/mod-manifest.schema.json` and declare identity,
semantic version, Lua entry point, loader API version, supported builds,
dependencies, ordering constraints, and requested capabilities.

## Manifest reference

Required fields:

| Field | Contract |
| --- | --- |
| `id` | Stable lowercase dotted ID, maximum 128 characters, e.g. `yourname.my-mod`; first component starts with a letter |
| `name`, `author` | Nonempty display strings, at most 128 characters each |
| `version` | Semantic version, e.g. `1.0.0` or `1.1.0-beta.1` |
| `entry_point` | Relative path to a packaged `.lua` script, normally `lua/main.lua` |
| `loader_api` | Integer `1` |
| `supported_game_builds` | Nonempty array of verified build IDs; currently `steam-win32-4.8` |
| `dependencies` | Array of `{ "id": "author.mod", "version": "^1.0.0" }`, or `[]` |
| `load_order` | Object containing both `before` and `after` ID arrays, even when empty |
| `capabilities` | Array of supported capability names, or `[]` |

Optional fields:

| Field | Contract |
| --- | --- |
| `$schema` | Editor schema location; not a runtime dependency or network fetch |
| `configuration_defaults` | Object of initial settings; use booleans, strings, and finite numbers for manager controls |
| `localization` | List of packaged `localization/*.json` paths; the current runtime specifically reads `localization/en-US.json` |
| `documentation` | Relative path to packaged documentation, normally `README.md` |

Unknown top-level fields are rejected. IDs and versions determine installation
identity. A package already installed at an ID/version cannot be replaced with
different bytes; increment the version and select the new version in a profile.
Changing a display name does not create a new identity.

### Capabilities

| Name | Declare when the mod uses |
| --- | --- |
| `gameplay.events` | Gameplay subscriptions and observation |
| `gameplay.mutate` | Cancellation, payload replacement, or validated live setters |
| `content.towers` | Custom tower registration |
| `content.assets` | Packaged custom content assets |
| `storage` | Private persistent string storage |

These declare intent and appear in manager details. They are not a per-call
permission system; adding an unknown name does not unlock APIs. Lua isolation
and validation apply independently. Logging, configuration, and timers need no
special capability.

### Configuration, localization, and resources

The manager edits supported scalar settings from `configuration_defaults`,
stores overrides per profile, and resets them to package defaults when asked.
`btd5.config.get` returns strings (`"true"`, `"12"`, etc.), or `nil` when absent.
The script must parse and bound numeric values. Restart after changing settings.

Ship `localization/en-US.json` as a flat JSON object of string keys and values.
Missing keys return the key itself. Additional locale selection, fallbacks,
and formatting placeholders are not implemented. Resource files are read with
`btd5.resource.read_text("assets/note.txt")`, limited to 1 MiB per read.
See [Resource Notes](../samples/resource-notes-mod/README.md).

## Allowed layout

Files may appear in `lua/`, `assets/`, `localization/`, `config/`, and `docs/`.
The root may contain `mod.json`, `README.md`, `CHANGELOG.md`, `LICENSE`, or
`LICENSE.md`.

```text
my-mod.btd5mod
├── mod.json
├── README.md
├── lua/main.lua
├── assets/                 # original textures, WAV files, or text data
└── localization/en-US.json
```

Do not wrap these entries in a parent folder. Extra Lua files are not executed
automatically, and the sandbox does not provide `require` or `dofile`.

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

Dependencies require the named package to be **enabled** in the profile at a
compatible version and load it first. An installed but disabled package does
not satisfy a dependency. `before`/`after` constraints are conditional: an absent
target is ignored. They cannot satisfy a missing dependency or import another
mod's Lua globals. A profile selects only one enabled version for each ID.

### Dependency versions

| Requirement | Meaning |
| --- | --- |
| `"*"` | Any version accepted by the runtime resolver |
| `"1.2.3"` | Exact semantic version |
| `">=1.2.3"` | Numeric major/minor/patch at least `1.2.3` |
| `"^1.2.3"` | At least `1.2.3`, same major |
| `"^0.2.3"` | At least `0.2.3`, same major and minor |
| `"^0.0.3"` | Same numeric `0.0.3` patch |

This is a small resolver, not npm's full range language: no `~`, combined
ranges, or OR expressions. Use the simple release-version forms above. The
JSON schema accepts these release-version forms and the bare wildcard.
Prerelease comparison for ranges is based on the numeric version, so do not
assume full prerelease ordering.

For example, this requires Event Monitor and requests that another optional
observer load later:

```json
{
  "dependencies": [{ "id": "example.event-monitor", "version": "^1.0.0" }],
  "load_order": { "before": ["example.resource-notes"], "after": [] }
}
```

This is a manifest fragment; include all other required fields. Avoid redundant
or contradictory order rules. The manager rejects cycles before launch.

## Validate and distribute

1. Build the archive with root-level `mod.json`.
2. Run `btd5loader_validate.exe package.btd5mod`, optionally with `--game` for
   art checks, and inspect warnings as well as the exit code.
3. Import it into the manager and check its complete profile dependencies.
4. Launch offline and test lifecycle/gameplay behavior; standalone validation
   runs entry scripts and registration only.
5. Include instructions, version/build requirements, settings, license, credits,
   and removal/save implications in the packaged README.

See the [step-by-step packaging tutorial](mod-authoring.md#4-package-the-contents).
