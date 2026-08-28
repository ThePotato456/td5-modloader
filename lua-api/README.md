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
