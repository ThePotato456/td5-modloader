# BTD5 Mod Loader

[![Development status](https://img.shields.io/badge/status-pre--alpha-orange)](ACTIONPLAN.md)
[![Platform](https://img.shields.io/badge/platform-Windows%20x86-0078D4?logo=windows)](docs/building.md)
[![C++](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus)](CMakeLists.txt)
[![Lua](https://img.shields.io/badge/Lua-5.4.9-2C2D72?logo=lua)](THIRD_PARTY_NOTICES.md)
[![.NET](https://img.shields.io/badge/.NET-10.0-512BD4?logo=dotnet)](global.json)
[![License: GPL v3](https://img.shields.io/badge/license-GPL--3.0--only-blue)](LICENSE)

An open-source, offline-focused mod loader for the 32-bit Windows Steam release
of **Bloons TD 5**. Native integration is implemented in C++20, while all public
mods use isolated, sandboxed Lua 5.4 environments.

> [!IMPORTANT]
> This project is under active development and is not ready for normal gameplay.
> Network blocking, save backups, crash recovery, gameplay mutation, and custom
> towers are roadmap features and must not be treated as complete yet. A limited
> set of lifecycle, cash, lives, tower, and bloon notifications is live for
> development testing.

BTD5 Mod Loader is an unofficial community project. It is not affiliated with,
endorsed by, or sponsored by Ninja Kiwi. Bloons TD 5 and its assets remain the
property of their respective owners.

## Project goals

- Provide one stable, documented Lua API instead of exposing native addresses or
  a public DLL plugin ABI.
- Allow Lua mods to add gameplay behavior and original content, including custom
  towers, upgrades, projectiles, artwork, localization, and audio.
- Fail closed when the installed game build is unknown or required hooks cannot
  be validated.
- Keep installation reversible without patching `BTD5-Win.exe` or `BTD5.jet`.
- Isolate mods with separate Lua states, restricted standard libraries, memory
  limits, instruction budgets, time limits, and contained callback failures.
- Support local `.btd5mod` packages, named profiles, dependency validation, safe
  recovery, and an approachable Windows manager.

## Current status

The repository currently includes:

- a reversible WinINet proxy and separate native loader runtime;
- executable and asset fingerprinting for the Steam Win32 4.8 build;
- validated symbol maps and transactional hook rollback;
- an embedded Lua 5.4.9 host with isolated mod states and sandbox limits;
- validated `.btd5mod` ZIP packages and deterministic dependency ordering;
- a WPF manager with Steam discovery, safe loader install/verify/repair/uninstall,
  package inspection, profiles, launch controls, logs, and diagnostics export;
- named profile persistence, per-mod configuration, deterministic profile order,
  dependency-safe operations, and bounded launch history;
- a validated manager-to-runtime handoff that loads packages, invokes Lua
  `on_load` and `on_ready`, and advances timers from a transactional render hook;
- live Lua match, round, cash, verified lives-changed, and post-action tower
  events with lifetime-checked tower wrappers on the supported build;
- live post-action bloon spawned, popped, and leaked events with
  lifetime-checked bloon wrappers;
- native and managed integration tests that do not redistribute game files.

Detailed progress and mandatory implementation gates are maintained in the
[implementation action plan](ACTIONPLAN.md).

## Architecture

| Component | Technology | Responsibility |
| --- | --- | --- |
| Bootstrap | C++20, Win32 | Loads the real WinINet library and starts the separate runtime |
| Runtime | C++20 | Compatibility checks, hooks, lifecycle, Lua host, and game integration |
| Public mod API | Lua 5.4 | Sandboxed lifecycle scripts, configuration, storage, localization, and future gameplay/content APIs |
| Manager | C# / WPF / .NET 10 | Installation, packages, profiles, launch workflow, diagnostics, and recovery |
| Mod package | ZIP / JSON / Lua | Portable `.btd5mod` archive containing a manifest, scripts, assets, localization, and documentation |

Native code is an internal implementation detail. Version 1 mods will be
Lua-only, including mods that register new towers.

## Building from source

### Requirements

- Windows 10 or newer;
- Visual Studio Build Tools with MSVC and CMake support;
- Git and network access for the initial pinned-dependency restore;
- the project-local .NET 10.0.400 SDK, or that exact SDK on `PATH`.

Install the local SDK and run the standard development gates from PowerShell:

```powershell
./scripts/install-dotnet.ps1
./scripts/build.ps1 -Configuration Debug
./scripts/test.ps1 -Configuration Debug
./scripts/analyze.ps1
./scripts/check-format.ps1
```

Use `-Configuration Release` for an optimized build. Native targets are always
Win32/x86 because they load into the 32-bit game process. The WPF manager is
architecture-neutral.

See the complete [build guide](docs/building.md) for toolchain details and the
read-only compatibility inspector.

## Game installation and local testing

The game is not required to build or run the automated test suites. Integration
testing requires a legally obtained Steam installation of Bloons TD 5.

You may use the normal Steam installation or an ignored local test copy. Keep a
local copy under `.local/game/` and put machine-specific settings in
`config/local.json`; both locations are excluded from Git. The loader must never
commit, redistribute, or silently modify the game's executable or asset archive.

Only explicitly fingerprinted builds are accepted. The currently supported
target and its validation record are documented in
[Steam Win32 4.8 validation](docs/validation-steam-win32-4.8.md).

## Creating a Lua mod

A mod is distributed as a `.btd5mod` ZIP archive with `mod.json` at its root:

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

The manifest declares the mod ID, semantic version, Lua entry point, loader API,
supported game builds, dependencies, ordering constraints, and requested
capabilities. Invalid or unsafe archives are rejected before the game starts.

Start with:

- the [Lua API reference](lua-api/README.md);
- the [package format specification](docs/mod-packages.md);
- the [manifest schema](schemas/mod-manifest.schema.json); and
- the [lifecycle sample mod](samples/lifecycle-mod/README.md).

The current sample demonstrates lifecycle callbacks, logging, configuration,
localization, deterministic timers, private mod storage, and the currently live
match, round, cash, post-change lives, tower placed/upgraded/sold, and bloon
spawned/popped/leaked events. Tower events include a stable, opaque tower
wrapper, and bloon events include an opaque wrapper with verified removal
invalidation. A custom-tower example will be added when its implementation gate
passes.

## Documentation

| Document | Purpose |
| --- | --- |
| [Implementation action plan](ACTIONPLAN.md) | Ordered roadmap, progress markers, and phase gates |
| [Build guide](docs/building.md) | Prerequisites, build commands, tests, analysis, and symbol inspection |
| [Lua API](lua-api/README.md) | Available sandboxed Lua functions and lifecycle behavior |
| [Gameplay event contract](docs/gameplay-events.md) | Event names, ordering, containment, and object lifetime rules |
| [Mod package format](docs/mod-packages.md) | Archive layout, validation limits, and deterministic ordering |
| [Manager storage and ownership](docs/manager-storage.md) | Safe installation, repair, uninstall, and local state rules |
| [Supported build validation](docs/validation-steam-win32-4.8.md) | Fingerprints and manual compatibility evidence |
| [Phase 5 validation](docs/validation-phase5.md) | Manager workflow and live Lua `on_load` acceptance evidence |
| [Phase 6 foundation validation](docs/validation-phase6-foundation.md) | Live render hook, `on_ready`, and timer acceptance evidence |
| [Phase 6 match-entry validation](docs/validation-phase6-match-entry.md) | First live Lua gameplay-event acceptance evidence |
| [Phase 6 match lifecycle validation](docs/validation-phase6-match-lifecycle.md) | Live match entry and teardown acceptance evidence |
| [Phase 6 round lifecycle validation](docs/validation-phase6-round-lifecycle.md) | Live native round-event acceptance evidence |
| [Phase 6 cash validation](docs/validation-phase6-cash.md) | Live native cash-notification acceptance evidence |
| [Phase 6 lives validation](docs/validation-phase6-lives-changed.md) | Verified post-change lives notification evidence |
| [Phase 6 tower-action validation](docs/validation-phase6-tower-actions.md) | Live tower placed, upgraded, and sold evidence |
| [Phase 6 bloon-action validation](docs/validation-phase6-bloon-actions.md) | Live bloon spawned, popped, and leaked evidence |
| [Upstream code provenance](docs/upstream-code-provenance.md) | Auditable record of GPL-covered adaptations |
| [Upstream research policy](docs/upstream-research.md) | Boundaries for research-only upstream material |
| [Third-party notices](THIRD_PARTY_NOTICES.md) | Pinned dependencies and their licenses |

## Safety and project boundaries

- Do not use the unfinished loader in online, ranked, leaderboard, or multiplayer
  contexts.
- Do not report a build as supported without executable and asset fingerprints
  plus passing symbol validation.
- Do not commit game binaries, assets, saves, Steam account data, installed mods,
  logs, backups, credentials, or machine-specific paths.
- Do not write custom identifiers into vanilla progression data.
- Treat every mod package as untrusted input and preserve sandbox boundaries.

Offline enforcement and save protection are planned for Phase 9. Until that gate
passes, “offline-focused” describes the project policy and design—not a completed
technical guarantee.

## Contributing

Review [ACTIONPLAN.md](ACTIONPLAN.md) before starting work. Phases are completed
in order, and each phase has an implementation gate that must pass before the
next phase begins. Changes should include proportionate tests, preserve the
fail-closed compatibility model, and avoid all proprietary game content.

Bug reports should include loader/manager logs, the loader version, and the
detected build ID. Do not attach game executables, assets, saves, or account data.

## License

BTD5 Mod Loader is licensed under the
[GNU General Public License v3.0 only](LICENSE).

GPL-covered code adapted from NKHook5 is documented with upstream paths and
commit provenance. BTD5-Decomp is research-only and contributes no copied or
adapted source. See [third-party notices](THIRD_PARTY_NOTICES.md) and
[upstream provenance](docs/upstream-code-provenance.md) for details.
