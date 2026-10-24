# Compatibility and current limits

[Documentation home](README.md)

## Supported versions

| Component | Current contract |
| --- | --- |
| Game | Steam Windows 32-bit 4.8, build ID `steam-win32-4.8` |
| Loader API | Manifest `loader_api: 1` |
| Lua | Sandboxed Lua 5.4; the repository pins its runtime dependency |
| Native loader | Win32/x86; loaded into the 32-bit game |
| Manager | Windows WPF; .NET 10 source build, exact SDK in `global.json` |

The manager and runtime compare executable and asset SHA-256 fingerprints and
validate symbols/instructions. A matching version label alone is insufficient.
Other storefronts and builds need independently verified maps. Never edit a
supported-build list or fingerprint to bypass a compatibility error.
The [symbol map](../symbols/btd5-steam-4.8.json) contains the exact fingerprints
and required patterns. See [symbol maps](../symbols/README.md) and
[native internals](native-internals.md) for address and layout conventions.

## Supported behavior and limits

| Available now | Boundary |
| --- | --- |
| Lua lifecycle, timers, configuration, storage, resources | Each mod has its own sandbox; timers count rendered frames |
| 28 gameplay events | Cancellation and mutation apply only to documented boundaries |
| Match, round, tower, bloon, player, attack, projectile wrappers | Only the methods listed in the [API](../lua-api/reference.md) exist |
| Custom tower stats, weapons, upgrades, callbacks, art, sounds | Register while the entry script loads; content is validated |
| Per-profile configuration and installed versions | Restart the game after a change; no hot reload |
| English string tables | Runtime reads `localization/en-US.json`; locale selection and a formatting pipeline are not implemented |
| Custom-tower progression and saved identity handling | Does not provide general save isolation, backups, or restoration |
| Manager readiness checks and offline acknowledgement | Technical network enforcement is not implemented |

General asset caching, automatic save backups, and crash recovery are not
implemented.

## Saves and mod identities

Custom towers use stable `modId:towerId` identities. The loader keeps custom
tower XP under `storage/custom-towers/progress.json` in its data root and keeps
custom IDs out of vanilla `Profile.save`. Match saves naming unavailable custom
towers are held aside until their mods return. Re-enable the same IDs to resume;
renaming an ID creates different content even if the display name is unchanged.

Private Lua storage is keyed by mod ID, shared between that mod's versions and
profiles. Per-profile configuration is separate. Changing `id` starts a new
storage namespace; upgrading the version does not reset it. Plan migrations for
your own stored strings and never store live wrapper IDs as durable identities.

Modded gameplay can still affect game saves or progression outside the custom
tower mechanisms above. Back up your own data before experiments. Crashes and
forced termination can skip shutdown and cleanup; automatic recovery is unavailable.

## Interoperability

Dependencies ensure an enabled, compatible package loads first. `before` and
`after` affect order only when their target is enabled. They do not import Lua
code or share globals across mod states. Cancellation is sticky across mods;
later mods cannot reverse an earlier cancellation. See the
[package specification](mod-packages.md) and [event contract](gameplay-events.md).

Capabilities describe a package's intent. They are not an operating-system
permission prompt or a substitute for reviewing a mod. The restricted Lua host
and validated API boundaries provide the implementation's isolation.
