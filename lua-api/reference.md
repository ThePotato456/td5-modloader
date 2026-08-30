# Lua API v1 signature reference

This document is the compact Markdown reference for the public Lua API. The
[main guide](README.md) explains usage and behavior; the documentation-only
[`btd5.lua`](btd5.lua) file provides equivalent editor type information.

## Conventions

| Notation | Meaning |
| --- | --- |
| `string?` | A string or `nil` |
| `integer` | A Lua 5.4 integer |
| `callback()` | A Lua function receiving no arguments |
| `callback(event)` | A Lua function receiving one event table |
| `GameObject` | Opaque, lifetime-checked userdata |
| `EventToken` | Positive integer returned by `btd5.events.on` |

Functions raise a contained Lua error for invalid argument types, invalid
paths, exceeded limits, or stale objects unless a different return value is
documented below.

## Optional lifecycle callbacks

```lua
function on_load() end
function on_ready() end
function on_shutdown() end
```

| Callback | Called |
| --- | --- |
| `on_load()` | Once while the enabled profile is loading |
| `on_ready()` | Once on the first rendered frame after mod loading |
| `on_shutdown()` | Once during orderly loader shutdown |

## `btd5`

### `btd5.log`

```lua
btd5.log(level: string, message: string): nil
```

Writes a structured runtime log record. The `error` level is recorded as an
error; other strings currently map to an informational record.

### `btd5.config`

```lua
btd5.config.get(key: string): string?
```

Returns the selected profile value serialized as a string, or `nil` when the
key is absent.

### `btd5.storage`

```lua
btd5.storage.get(key: string): string?
btd5.storage.set(key: string, value: string): nil
```

Storage is private to the mod ID and persists between launches. Keys are at
most 128 bytes and values are at most 64 KiB.

### `btd5.localization`

```lua
btd5.localization.get(key: string): string
```

Returns the localized value or the original key when no translation exists.

### `btd5.resource`

```lua
btd5.resource.read_text(path: string): string
```

Reads up to 1 MiB from a packaged, relative resource path. Paths use `/` and
cannot contain empty, `.`, or `..` components.

### `btd5.timer`

```lua
btd5.timer.after(ticks: integer, callback: function): nil
```

Schedules a one-shot callback. `ticks` must be non-negative. One tick currently
equals one rendered frame.

### `btd5.events`

```lua
btd5.events.on(name: EventName, callback: function): EventToken
btd5.events.off(token: EventToken): boolean
```

`off` returns `true` only when it removes an active subscription. Each mod may
have at most 256 active handlers.

## `GameObject`

```lua
object:is_valid(): boolean
object:id(): integer
object:kind(): "tower" | "bloon"
```

`is_valid()` is safe on stale objects. `id()` and `kind()` raise an error after
the native object is destroyed or its scene ends.

## Base event table

Every handler receives:

```lua
event = {
    name = "event.name",
    cancelled = false
}
```

`cancelled` is shared between handlers for the same mod. Only
`lives.changing` currently consumes it. Assigning it on another event has no
gameplay effect.

## Event payloads

### Lifecycle and economy notifications

These events add no payload fields:

- `match.starting`, `match.started`, `match.ending`, `match.ended`;
- `round.starting`, `round.started`, `round.ending`, `round.ended`; and
- `cash.changing`, `cash.changed`.

### Lives events

```lua
event.old_lives: integer
event.new_lives: integer
```

| Event | Cancellable |
| --- | --- |
| `lives.changing` | Yes |
| `lives.changed` | No |

### Tower events

```lua
event.tower: GameObject
```

- `tower.placing`, `tower.placed`;
- `tower.upgrading`, `tower.upgraded`; and
- `tower.selling`, `tower.sold`.

Tower payloads and wrappers are currently read-only. `tower.upgrading` is
cancellable; setting `event.cancelled = true` rejects the pending upgrade before
its first mutation and suppresses `tower.upgraded`. The wrapper passed to
`tower.sold` becomes stale after all handlers return.

### Bloon events

```lua
event.bloon: GameObject
```

- `bloon.spawning`, `bloon.spawned`;
- `bloon.popping`, `bloon.popped`; and
- `bloon.leaking`, `bloon.leaked`.

All bloon events are currently read-only. Wrappers passed to `bloon.popped` and
`bloon.leaked` become stale after all handlers return.

## Sandbox availability

Available standard libraries:

- base;
- coroutine;
- math;
- string;
- table; and
- UTF-8.

Unavailable facilities include `io`, `os`, `package`, `debug`, `require`,
`load`, `loadfile`, `dofile`, `collectgarbage`, native DLL loading, process
launch, arbitrary filesystem access, and networking.

## Not yet implemented

The following are planned but are not valid v1 calls today:

- gameplay-property getters and setters;
- cancellation for tower or bloon actions;
- cash balance payloads or mutation; and
- `btd5.towers.register` and the custom tower content API.
