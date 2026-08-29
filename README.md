# BTD5 Mod Loader

[![Status: pre-alpha](https://img.shields.io/badge/status-pre--alpha-F59E0B)](ACTIONPLAN.md)
[![Platform: Windows x86](https://img.shields.io/badge/platform-Windows%20x86-0078D4?logo=windows)](docs/building.md)
[![C++ 20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus)](CMakeLists.txt)
[![Lua 5.4.9](https://img.shields.io/badge/Lua-5.4.9-2C2D72?logo=lua)](lua-api/README.md)
[![.NET 10](https://img.shields.io/badge/.NET-10-512BD4?logo=dotnet)](global.json)
[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue)](LICENSE)

An open-source, offline-focused mod loader for the 32-bit Windows Steam edition
of **Bloons TD 5**. The loader integrates with the game through a native C++20
runtime while giving mod authors a sandboxed Lua 5.4 API.

> [!WARNING]
> BTD5 Mod Loader is in pre-alpha development. It can load Lua mods and interact
> with a verified game build, but it does not yet provide network enforcement,
> automatic save backups, crash recovery, general gameplay mutation, or custom
> towers. Do not use it for online, ranked, leaderboard, or multiplayer play.

This is an unofficial community project. It is not affiliated with or endorsed
by Ninja Kiwi. Bloons TD 5 and its assets belong to their respective owners.

## What works today

| Area | Status | Current capability |
| --- | --- | --- |
| Windows manager | Working | Finds Steam installations; installs, verifies, repairs, and removes loader-owned files |
| Mod packages | Working | Validates and installs local `.btd5mod` archives with dependencies and deterministic load order |
| Profiles | Working | Enables, disables, reorders, upgrades, and launches configured sets of mods |
| Lua runtime | Working | Isolated Lua states, lifecycle callbacks, timers, configuration, localization, storage, and resource reads |
| Gameplay events | Working | Live match, round, cash, lives, tower, and bloon notifications |
| Object wrappers | Partial | Stable, lifetime-checked tower and bloon handles are exposed to Lua |
| Event cancellation | Partial | `lives.changing` can cancel a verified life gain or loss before the native write |
| Mutable gameplay | Planned | Validated property setters and mutable event fields are not implemented |
| Custom towers | Planned | Lua-defined towers, upgrades, attacks, and assets begin after the Phase 6 gate |
| Offline protection | Planned | Network guards, save backups, and crash recovery are Phase 9 work |

Progress is tracked by checked implementation steps and mandatory phase gates in
[ACTIONPLAN.md](ACTIONPLAN.md).

## Design

- **Lua is the public mod format.** Mod authors do not need to compile C++ or
  depend on a native plugin ABI.
- **C++ stays behind the API boundary.** Hooking, compatibility checks, object
  lifetime tracking, and Lua hosting remain loader implementation details.
- **Unknown builds fail closed.** The runtime checks game fingerprints, resolves
  named symbols, validates expected instructions, and refuses unsafe hooks.
- **Installation is reversible.** Loader files are tracked separately;
  `BTD5-Win.exe` and `BTD5.jet` are not patched on disk.
- **Mods are isolated.** Each mod receives its own restricted Lua state with
  memory, instruction, recursion, and callback-time limits.
- **Packages are deterministic.** Dependencies, explicit ordering constraints,
  profile order, and stable tie-breaking produce a repeatable load order.

## Supported game build

The current compatibility target is **Steam Win32 4.8**.

The loader uses relative virtual addresses and runtime pattern validation, so
Windows ASLR does not require fixed process addresses. Another computer can use
the same compatibility map when its game executable and asset archive are
byte-for-byte identical to the verified build. A different update, storefront
edition, regional binary, or modified executable requires a separately tested
symbol map.

Compatibility is checked before gameplay hooks are installed. A mismatch stops
mod loading instead of attempting to reuse uncertain offsets. See the
[supported-build validation record](docs/validation-steam-win32-4.8.md).

## Lua mod example

Every enabled mod runs in its own Lua environment. This example observes life
changes and optionally cancels life loss:

```lua
btd5.events.on("lives.changing", function(event)
    btd5.log(
        "info",
        "lives: " .. event.old_lives .. " -> " .. event.new_lives
    )

    if event.new_lives < event.old_lives then
        event.cancelled = true
    end
end)

function on_ready()
    btd5.log("info", "mod is ready")
end
```

Cancelling `lives.changing` skips only the pending lives write. It does not undo
the originating bloon leak or reward action, and a cancelled transition does
not emit `lives.changed`.

Start with the [Lua API reference](lua-api/README.md), the
[gameplay event contract](docs/gameplay-events.md), and the complete
[lifecycle sample](samples/lifecycle-mod/README.md).

## Mod package layout

A mod is a ZIP-compatible archive using the `.btd5mod` extension, with
`mod.json` at its root:

```text
example.btd5mod
├── mod.json
├── lua/
│   └── main.lua
├── assets/
├── localization/
│   └── en-US.json
└── README.md
```

The manifest declares identity, version, entry point, loader API compatibility,
supported game builds, dependencies, ordering constraints, capabilities, and
configuration defaults. Unsafe paths, malformed manifests, unsupported files,
oversized content, dependency errors, and incompatible builds are rejected
before launch.

See the [package specification](docs/mod-packages.md) and
[manifest schema](schemas/mod-manifest.schema.json).

## Architecture

| Component | Technology | Responsibility |
| --- | --- | --- |
| Bootstrap | C++20 / Win32 | Proxies the real system WinINet library and starts the runtime |
| Runtime | C++20 | Fingerprinting, symbol resolution, hooks, object tracking, and Lua hosting |
| Public mod API | Lua 5.4 | Sandboxed scripts, lifecycle, events, storage, configuration, and future content registration |
| Manager | C# / WPF / .NET 10 | Installation, packages, profiles, launch controls, logs, and diagnostics |
| Mod package | ZIP / JSON / Lua | Portable scripts, metadata, localization, documentation, and original assets |

Native plugins are not part of the public v1 format. Future custom towers will
also be registered through Lua.

## Building from source

### Requirements

- Windows 10 or later;
- Visual Studio Build Tools with the MSVC x86/x64 workload and bundled CMake;
- Git and network access for the first pinned-dependency restore; and
- the project-local .NET 10.0.400 SDK, or that exact SDK on `PATH`.

From PowerShell:

```powershell
./scripts/install-dotnet.ps1
./scripts/build.ps1 -Configuration Debug
./scripts/test.ps1 -Configuration Debug
./scripts/analyze.ps1
./scripts/check-format.ps1
./scripts/stage.ps1 -Configuration Debug
```

The staged manager, runtime, compatibility maps, and sample package are written
to `out/stage/debug`. Use `-Configuration Release` for optimized artifacts.
Native targets are always Win32/x86 because they load into BTD5's 32-bit
process; the manager itself is architecture-neutral.

The automated suites do not require the game. See the
[build guide](docs/building.md) for complete toolchain and symbol-inspection
instructions.

## Local game testing

Integration tests require a legally obtained Steam installation. You may point
the tools at the normal Steam directory or place an ignored test copy under
`.local/game/`. Machine-specific settings belong in ignored
`config/local.json`.

Never commit or redistribute the game executable, assets, saves, Steam account
data, installed mods, logs, or local paths. The repository intentionally
contains no Ninja Kiwi files.

## Documentation

- [Implementation roadmap and gates](ACTIONPLAN.md)
- [Building and compatibility inspection](docs/building.md)
- [Lua API reference](lua-api/README.md)
- [Gameplay events and wrapper lifetimes](docs/gameplay-events.md)
- [Mod package format](docs/mod-packages.md)
- [Manager storage and file ownership](docs/manager-storage.md)
- [Supported Steam build validation](docs/validation-steam-win32-4.8.md)
- [Live lives-event and cancellation validation](docs/validation-phase6-lives-changed.md)
- [Tower-event validation](docs/validation-phase6-tower-actions.md)
- [Bloon-event validation](docs/validation-phase6-bloon-actions.md)
- [Upstream code provenance](docs/upstream-code-provenance.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

Additional milestone evidence is kept under [`docs/`](docs/).

## Safety boundaries

- Treat every mod package as untrusted input.
- Do not claim compatibility for an unverified executable or asset fingerprint.
- Do not use the loader in networked or competitive contexts.
- Do not write mod identifiers or custom data into vanilla progression files.
- Do not distribute proprietary game content with the loader or a mod.

“Offline-focused” currently describes project scope and policy. It is not yet a
technical guarantee; fail-closed network enforcement and save protection remain
planned work.

## Contributing

Read [ACTIONPLAN.md](ACTIONPLAN.md) before starting. Work proceeds in phase order,
and each phase has an implementation gate that must pass before the next phase
begins. Changes should include proportionate tests, preserve transactional hook
rollback, and keep all compatibility claims tied to verified evidence.

Bug reports should include the loader version, detected build ID, and relevant
loader or manager logs. Do not attach game executables, assets, saves, account
identifiers, or credentials.

## License and upstream work

BTD5 Mod Loader is licensed under the
[GNU General Public License v3.0 only](LICENSE).

GPL-covered adaptations from NKHook5 are recorded with their source paths and
commit provenance. BTD5-Decomp is used only as research material; no source is
copied from it. See [upstream provenance](docs/upstream-code-provenance.md),
[research policy](docs/upstream-research.md), and
[third-party notices](THIRD_PARTY_NOTICES.md).
