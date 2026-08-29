# Lua API v1

Each enabled mod receives its own Lua 5.4 state. States do not share globals,
registry entries, timers, configuration, or storage.

## Lifecycle

A mod may define `on_load()`, `on_ready()`, and `on_shutdown()`. A callback
error is annotated with the mod ID and callback name. The failing callback is
disabled; other callbacks and other mod states remain available.

The live manager-to-runtime bridge invokes `on_load()` during profile loading.
It invokes `on_ready()` once on the first rendered frame after all enabled mods
load, then advances deterministic timers once per rendered frame. Mods must not
infer that the game scene is ready from `on_load()`.

## Sandboxed host API

- `btd5.log(level, message)` writes through the loader log sink.
- `btd5.config.get(key)` reads profile-provided configuration.
- `btd5.storage.get(key)` and `set(key, value)` access private string storage.
- `btd5.localization.get(key)` resolves a localized string or returns the key.
- `btd5.resource.read_text(path)` reads a packaged text resource up to 1 MiB.
- `btd5.timer.after(ticks, callback)` schedules a deterministic one-shot timer.
  In the current live host, one tick is one rendered frame rather than one game
  simulation step.
- `btd5.events.on(name, callback)` subscribes and returns an integer token.
- `btd5.events.off(token)` removes a subscription and reports whether it existed.

## Gameplay events and object wrappers

The v1 event names and wrapper lifetime rules are specified in the
[gameplay event contract](../docs/gameplay-events.md). Handlers run in mod load
order and then subscription order. A handler added during dispatch begins with
the next event; removing a handler takes effect immediately. A failing handler
is disabled without stopping later handlers.

Game objects are opaque userdata. `object:is_valid()` is the only operation
allowed on a stale object. `object:id()` and `object:kind()` reject handles whose
native object was destroyed, reused, or belonged to an earlier scene.

The event bus and wrappers are mock-host validated. `match.starting`,
`match.started`, `match.ending`, and `match.ended` currently fire in the
supported game. `round.starting`, `round.started`, `round.ending`, and
`round.ended` are also live. `cash.changing` and `cash.changed` fire around the
native money-update observer dispatch, but currently have no balance payload or
mutation support. The remaining names do not yet have live hooks. Mods must not
infer support for an event merely because the API accepts its name.

Resource paths must use `/`, remain relative, and contain no `.` or `..`
components. Mods never receive a general filesystem path.

## Removed facilities

The sandbox does not open `io`, `os`, `package`, or `debug`. It also removes
`require`, `load`, `loadfile`, `dofile`, and `collectgarbage`. Consequently a
mod has no host-provided route to arbitrary files, processes, networking,
Windows APIs, or native DLL loading.

## Default limits

- 16 MiB memory per state.
- 250,000 virtual-machine instructions and 100 milliseconds per callback.
- 32 Lua frames per callback.
- 128-byte storage keys and 64 KiB storage values.

A limit violation becomes a normal contained callback error.
