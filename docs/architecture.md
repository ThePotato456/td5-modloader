# Architecture and source map

[Documentation home](README.md) · [Contributing](../CONTRIBUTING.md)

The manager prepares a validated profile. A WinINet proxy boots the native
runtime inside the game. The runtime accepts a process-bound launch ticket,
checks the supported game, installs verified hooks, and hosts one Lua state per
enabled mod. The public extension point is Lua; native DLL plugins are not part
of the v1 package format.

## Launch and shutdown

1. The manager validates the selected game, loader ownership, packages,
   configuration, dependencies, and resolved load order.
2. Steam starts the game normally. The manager observes the selected process
   and creates a single-use ticket bound to its PID, creation time, and path.
3. `wininet.dll` forwards system WinINet functions and starts the runtime.
4. Without a matching ticket, runtime startup stays vanilla. With authorization,
   the runtime revalidates packages and extracts them to a per-process session.
5. Each enabled mod's entry script runs in its own Lua state. Top-level tower
   and sound registrations happen here, then `on_load` runs. Generated custom
   content is served through loader file hooks; the original asset archive stays
   unchanged.
6. The first rendered frame invokes `on_ready`. Subsequent frames advance timers;
   gameplay hooks dispatch subscribed events in resolved mod order.
7. Normal exit stops dispatch, invalidates objects, and invokes `on_shutdown` in
   reverse load order before Lua states are destroyed. Crashes can skip this path.

The [storage guide](manager-storage.md) describes files and ticket validation.
The [event contract](gameplay-events.md) describes action boundaries and wrappers.
The [native internals reference](native-internals.md) documents build-specific
pointers, calling conventions, and native code flow.

## Code map

| Path | Responsibility |
| --- | --- |
| `src/native/bootstrap/` | Proxy exports and runtime bootstrap |
| `src/native/runtime/runtime.cpp` | Runtime orchestration, compatibility, hook installation, mod loading |
| `src/native/runtime/lua_mod.*` | Restricted Lua host, APIs, budgets, lifecycle, timers, event dispatch |
| `src/native/runtime/mod_manifest.*`, `mod_package.*` | Manifest/version/order rules, ZIP validation and extraction |
| `src/native/runtime/game_object.*`, `object_events.hpp` | Lifetime-checked wrapper registry and event names |
| `src/native/runtime/*_hook.*` | Verified native boundaries for matches, rounds, lives, towers, bloons, objects |
| `src/native/runtime/tower_registry.*`, `custom_tower_*`, `tower_progress.*` | Content definitions, identities, art, sounds, saves, progression |
| `src/native/tools/validate_package.cpp` | Standalone script/content/package validator |
| `src/manager/BTD5ModLoader.Manager.Core/` | Testable installation, packages, profiles, launch, logging, diagnostics |
| `src/manager/BTD5ModLoader.Manager/` | WPF interface |
| `schemas/`, `symbols/`, `lua-api/` | Package schema, compatibility maps, author API contracts |
| `samples/`, `tests/`, `scripts/` | Examples, fixtures, build/stage/test/release and live acceptance tools |

## Design constraints for contributors

Native hooks must validate expected instructions and install transactionally;
partial failure must roll back. Object handles must become stale when native
lifetimes or scenes end. Invalid Lua arguments must fail at a protected boundary
without exposing native pointers. Nested host entry shares callback budgets.

Every mod has separate Lua globals and handlers, but hooks and native objects
belong to the game process. A sandbox does not make an arbitrary gameplay
mutation meaningful: expose only researched, validated operations with explicit
event timing and lifetime contracts.

When adding an API, update its runtime implementation, appropriate fixture
coverage, [signature reference](../lua-api/reference.md),
[editor definitions](../lua-api/btd5.lua), guide, and focused sample together.
Do not copy decompiled game source or game assets into this repository; see
[provenance](upstream-code-provenance.md).
