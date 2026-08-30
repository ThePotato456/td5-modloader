# BTD5 Mod Loader Lua API v1

Lua 5.4 is the public modding API. Every enabled mod runs in an isolated,
sandboxed Lua state with its own globals, subscriptions, timers, configuration,
storage, and resource directory.

This reference describes features implemented in the supported Steam Win32 4.8
build. Custom towers and general gameplay-property setters are planned but are
not part of the current API.

Use the compact [Markdown signature reference](reference.md) while authoring a
mod. For editor autocomplete and diagnostics, add the documentation-only
[`btd5.lua`](btd5.lua) definition file to your Lua Language Server workspace.
Do not package or execute that file with a mod.

## Quick start

A minimal mod contains two files:

```text
hello-world/
├── mod.json
└── lua/
    └── main.lua
```

`mod.json`:

```json
{
  "$schema": "../../schemas/mod-manifest.schema.json",
  "id": "example.hello-world",
  "name": "Hello World",
  "author": "Your name",
  "version": "1.0.0",
  "entry_point": "lua/main.lua",
  "loader_api": 1,
  "supported_game_builds": ["steam-win32-4.8"],
  "dependencies": [],
  "load_order": { "before": [], "after": [] },
  "capabilities": []
}
```

`lua/main.lua`:

```lua
function on_load()
    btd5.log("info", "Hello World loaded")
end

function on_ready()
    btd5.log("info", "The game render loop is ready")
end
```

Package the contents—not the containing directory—as a ZIP archive and rename
it to `.btd5mod`. The manager validates and installs the package. See the
[package specification](../docs/mod-packages.md) for manifest and archive rules.

## Execution model

The host opens Lua's base, table, string, math, UTF-8, and coroutine libraries.
Use `btd5.log` instead of relying on console output.

The following lifecycle functions are optional:

| Callback | Timing |
| --- | --- |
| `on_load()` | Runs while the enabled profile is loading. The game scene is not ready. |
| `on_ready()` | Runs once on the first rendered frame after all enabled mods load. |
| `on_shutdown()` | Runs during an orderly loader shutdown. |

A callback error is logged with the mod ID and callback name. The failing
callback is disabled when continuing is safe; other callbacks and mods continue.

## Host API reference

### Logging

```lua
btd5.log(level, message)
```

`level` and `message` must be strings. `"error"` produces an error record;
other levels currently produce an informational record.

### Configuration

```lua
local value = btd5.config.get(key)
```

Returns the profile configuration value as a string, or `nil` when the key is
missing. JSON booleans and numbers are serialized as strings, so parse them
explicitly:

```lua
local enabled = btd5.config.get("enabled") == "true"
local threshold = tonumber(btd5.config.get("threshold") or "10")
```

Defaults are declared in `mod.json` under `configuration_defaults`.

### Private storage

```lua
local value = btd5.storage.get(key)
btd5.storage.set(key, value)
```

Keys and values are strings. Missing keys return `nil`. Storage is private to
the mod ID and persists between launches. Keys are limited to 128 bytes and
values to 64 KiB. Mods using this API should declare the `storage` capability.

### Localization

```lua
local text = btd5.localization.get(key)
```

Returns the active localized value, or the key itself when no value exists.
Localization files are declared by the manifest and stored under
`localization/`.

### Packaged text resources

```lua
local contents = btd5.resource.read_text("assets/example.txt")
```

Reads a file from the extracted mod package. Paths must use `/`, remain
relative, and contain no `.` or `..` components. The file must exist and be no
larger than 1 MiB. This API never exposes a general filesystem path.

### Deterministic timers

```lua
btd5.timer.after(ticks, callback)
```

Schedules a one-shot callback after a non-negative number of ticks. Currently,
one tick is one rendered frame, not one simulation step or a fixed duration.
Timers with the same due tick run in registration order.

```lua
btd5.timer.after(60, function()
    btd5.log("info", "approximately 60 rendered frames passed")
end)
```

### Event subscriptions

```lua
local token = btd5.events.on(name, callback)
local removed = btd5.events.off(token)
```

`on` returns a positive integer subscription token. `off` returns `true` when
the active subscription was removed and `false` for an unknown or previously
removed token. A mod may have at most 256 active handlers.

Handlers run in enabled-mod load order, then subscription order. A handler added
during dispatch starts with the next event. Removing a handler takes effect
immediately. A failing handler is disabled without stopping later handlers.

```lua
local token
token = btd5.events.on("match.started", function(event)
    btd5.log("info", event.name)
    btd5.events.off(token)
end)
```

## Live gameplay events

Every handler receives one shared event table. It always contains `name` and
`cancelled`. Payload fields depend on the event.

| Event | Payload | Timing | Cancellable |
| --- | --- | --- | --- |
| `match.starting` | none | Before game-screen initialization | No |
| `match.started` | none | After game-screen initialization | No |
| `match.ending` | none | Before game-screen teardown | No |
| `match.ended` | none | After game-screen teardown | No |
| `round.starting` | none | Before native round-start observer dispatch | No |
| `round.started` | none | After native round-start observer dispatch | No |
| `round.ending` | none | Before native round-end observer dispatch | No |
| `round.ended` | none | After native round-end observer dispatch | No |
| `cash.changing` | none | Before native money-update observer dispatch | No |
| `cash.changed` | none | After native money-update observer dispatch | No |
| `lives.changing` | `old_lives`, `new_lives` | At the accepted native lives write | **Yes** |
| `lives.changed` | `old_lives`, `new_lives` | After a verified lives change | No |
| `tower.placing` | `tower` | Before tower-manager ownership | No |
| `tower.placed` | `tower` | After placement observer dispatch | No |
| `tower.upgrading` | `tower` | After eligibility and before upgrade mutation | **Yes** |
| `tower.upgraded` | `tower` | After upgrade observer dispatch | No |
| `tower.selling` | `tower` | After eligibility and before sale side effects | **Yes** |
| `tower.sold` | `tower` | After sale observer dispatch | No |
| `bloon.spawning` | `bloon` | Before bloon-manager ownership | No |
| `bloon.spawned` | `bloon` | After spawn observer dispatch | No |
| `bloon.popping` | `bloon` | After acceptance and before pop side effects | No |
| `bloon.popped` | `bloon` | After pop observer dispatch | No |
| `bloon.leaking` | `bloon` | After track-end acceptance and before leak side effects | No |
| `bloon.leaked` | `bloon` | After leak observer dispatch | No |

`old_lives` and `new_lives` are Lua integers. Tower and bloon payloads are
opaque game-object userdata.

### Cancelling a lives change or tower action

`lives.changing`, `tower.upgrading`, and `tower.selling` honor
`event.cancelled`:

```lua
btd5.events.on("lives.changing", function(event)
    if event.new_lives < event.old_lives then
        event.cancelled = true
    end
end)

btd5.events.on("tower.upgrading", function(event)
    if should_block_upgrade(event.tower) then
        event.cancelled = true
    end
end)

btd5.events.on("tower.selling", function(event)
    if should_keep_tower(event.tower) then
        event.cancelled = true
    end
end)
```

Cancellation skips the pending lives write and suppresses `lives.changed` for
that transition. It does not undo the originating bloon leak or reward action.
Cancelling `tower.upgrading` resumes the game's own rejected-upgrade path before
the first mutation, so `tower.upgraded` does not run. Cancelling `tower.selling`
uses the game's rejected-sale path before removal or refund side effects, so
`tower.sold` does not run. Setting `cancelled` on any other event currently has
no gameplay effect.

The detailed ordering contract is maintained in
[Gameplay event contract v1](../docs/gameplay-events.md).

## Game-object wrappers

Tower and bloon event payloads expose three methods:

```lua
object:is_valid()  -- boolean
object:id()        -- positive integer while valid
object:kind()      -- "tower" or "bloon" while valid
```

IDs are stable for one native object's lifetime. A tower keeps the same ID from
placing through sale. A bloon keeps the same ID from spawning through pop or
leak. Popped parent bloons and newly created child layers have different IDs.

`tower.sold`, `bloon.popped`, and `bloon.leaked` receive a valid wrapper during
the handler. The wrapper becomes stale after every handler for that event
returns. Match teardown invalidates remaining scene objects.

Always call `is_valid()` before using a wrapper retained beyond its immediate
handler. Calling `id()` or `kind()` on a stale wrapper raises a contained Lua
error. Raw native addresses are never exposed.

## Sandbox and limits

The sandbox does not expose `io`, `os`, `package`, or `debug`. It removes
`require`, `load`, `loadfile`, `dofile`, and `collectgarbage`. Mods have no
host-provided route to arbitrary files, processes, networking, Windows APIs, or
native DLL loading.

Default per-mod limits:

- 16 MiB of Lua-state memory;
- 250,000 virtual-machine instructions per callback;
- 100 milliseconds per callback;
- 32 Lua frames per callback;
- eight nested event dispatches; and
- 256 active event handlers.

A limit violation is handled as a normal contained callback error.

## Current limitations

- Tower and bloon pre-events are read-only and not yet cancellable.
- Cash events do not yet expose balance values.
- Game-object wrappers do not yet expose gameplay getters or setters.
- `btd5.towers.register` and custom tower content are planned for Phase 7 and do
  not exist yet.
- No networking or native plugin API is exposed to Lua.

## Editor setup

With the Lua Language Server extension installed, copy or link `lua-api/btd5.lua`
into a documentation folder in your mod workspace and add that folder to
`Lua.workspace.library`. The file is marked `---@meta`, so the language server
uses it for completion without treating it as normal runtime source.

Example `.luarc.json` for a mod developed inside this repository:

```json
{
  "runtime.version": "Lua 5.4",
  "workspace.library": ["../../lua-api"],
  "diagnostics.globals": ["btd5"]
}
```

The definitions intentionally describe only implemented v1 methods. If an API
is absent from `btd5.lua`, mods should not depend on it.

## Troubleshooting mod startup

The manager writes the selected profile to
`%LocalAppData%\BTD5ModLoader\runtime\active-profile.json` immediately before a
modded launch. It normally passes that path to the runtime through the
`BTD5ML_ACTIVE_PROFILE` environment variable.

Steam can start the game in a process that does not inherit the manager's
environment. Commit `c950737` fixed that launch path: when the environment
variable is absent, the runtime may use the manager-owned active-profile file
only if its modification time is no more than 60 seconds old. The freshness
limit prevents a later ordinary launch from silently reusing an old modded
profile.

Useful runtime log records:

- `active_profile_fallback`: the fresh manager handoff was recovered;
- `active_profile_fallback_stale`: an old handoff was deliberately ignored;
- `active_profile_fallback_timestamp_failed`: freshness could not be verified;
- `no_active_profile`: no manager handoff was available, so no Lua mods loaded.

Therefore, an installed mod producing no `on_load` record is first a profile
handoff/enablement issue, not necessarily a Lua script failure. Launch the
enabled profile from the manager and inspect these records before debugging the
script itself.

## Example mods

- [Hello World](../samples/hello-world-mod/README.md): lifecycle and timers.
- [Event Monitor](../samples/event-monitor-mod/README.md): subscriptions,
  object identity, counters, configuration, and storage.
- [Lives Guardian](../samples/lives-guardian-mod/README.md): the currently
  supported cancellation API.
- [Lifecycle Sample](../samples/lifecycle-mod/README.md): comprehensive runtime
  validation and every live event.
