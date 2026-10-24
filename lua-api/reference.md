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

On the supported build, normal return from the game main function triggers
shutdown before CRT teardown. Loaded mods receive callbacks in reverse load order;
a failed callback does not skip other mods. Startup rollback uses the same order.
Game-object handles are already invalid during shutdown; logging, storage, and
packaged-resource access remain available until callbacks finish. No further
frame, event, or timer dispatch is admitted once shutdown begins.

An explicit native shutdown request drains Lua work and destroys its states, but
keeps inert native hooks installed until normal game exit so an in-flight native
detour can finish safely. Unloading the runtime DLL while the game is running is
unsupported. Forced termination and crashes cannot guarantee shutdown callbacks
or session cleanup. Automatic crash recovery is not implemented.

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

### `btd5.towers`

```lua
btd5.towers.register(definition: TowerDefinition): true
```

Registers `modId:definition.id` at entry-script load time, before lifecycle
callbacks. Returns `true` or raises a contained error for an invalid definition.
The definition supports base/cost/text, stats, weapons, upgrades, art, rank/mode
rules, and callbacks. See the complete [tower field reference](../docs/custom-towers.md#registration-fields)
and [editor types](btd5.lua).

`on_place(tower, event)` maps to `tower.placed`, `on_upgrade(tower, event)` to
`tower.upgraded`, and `on_sell(tower, event)` to **`tower.selling`**. These filtered
subscriptions consume normal event handler slots. `on_sell` can cancel the sale;
it does not confirm that a sale completed.

### `btd5.sounds`

```lua
btd5.sounds.register({ id: string, files: string[], cooldown?: number }): true
btd5.sounds.play(id: string): boolean, string?
```

Register during entry-script load. `files` lists 1–8 existing `.wav` paths under
`assets/`, each at most 8 MiB. `cooldown` is `0..10` seconds, default `0.1`.
The game selects among variants randomly. Local IDs expand to `modId:id`; `play`
also accepts a full namespaced ID. Playback returns `true` on dispatch or `false`
and a reason when unknown or unavailable (including before match sounds load).
See [sound formats and workflow](../docs/custom-towers.md#5-register-and-play-sounds).

## `GameObject`

```lua
object:is_valid(): boolean
object:id(): integer
object:kind(): "match" | "round" | "player" | "tower" | "attack" | "projectile" | "bloon"
```

`is_valid()` is safe on stale objects. `id()` and `kind()` raise an error after
the native object is destroyed or its scene ends.

### `Tower`

```lua
tower:pop_count(): integer
tower:set_pop_count(value: integer): boolean
tower:sell_price(): integer
tower:set_sell_price(value: integer): boolean
tower:type(): string | nil
tower:upgrade_tiers(): integer, integer
```

`pop_count()` reads the live tower's accumulated pop count.
`set_pop_count()` accepts an integer in `0..2147483647`, applies it immediately,
and returns `true`. Both calls raise a contained error for a stale wrapper, the
wrong object kind, an invalid type or range, or an unsupported game build.
`sell_price()` reads the live base sale value consumed by the game's payout
calculation. `set_sell_price()` accepts an integer in `0..2147483647` and
changes that base value immediately.

`type()` returns the game's internal tower type name, such as `"DartMonkey"`,
`"SuperMonkey"`, or `"MonkeySub"`, a custom tower's public `modId:towerId`, or
`nil` for a type the supported build does not name. `upgrade_tiers()` returns
the left and right path tiers, each in
`0..4`; a new tower returns `0, 0`. Both are read-only.

### `Bloon`

```lua
bloon:health(): number
bloon:set_health(value: number): boolean
bloon:type(): string | nil
```

`health()` reads the live floating-point health value. `set_health()` accepts a
finite, nonnegative value up to the maximum finite 32-bit float and applies it
immediately. Setting health does not directly dispatch damage or pop events;
the game observes the replacement through its normal update and damage paths.

`type()` returns the bloon type name: `"Red"`, `"Blue"`, `"Green"`, `"Yellow"`,
`"Pink"`, `"Black"`, `"White"`, `"Lead"`, `"Zebra"`, `"Rainbow"`, `"Ceramic"`,
`"Golden"`, `"MOAB"`, `"BFB"`, `"ZOMG"`, `"Bloonarius"`, `"Blastapopoulos"`,
`"Vortex"`, `"Dreadbloon"`, or `"TestBloon"`; it returns `nil` for an unnamed
type. Camo, regrow, and fortified modifiers are not part of the type.

### `Round`

```lua
round:number(): integer
```

`number()` returns the round number the game is playing; the first round of a
match is `1`.

### `Match`

```lua
match:set_cash(value: number): boolean
```

`set_cash()` accepts a finite number in `0..2147483647` and sets the player's
cash through the game's own add or spend helper, so the on-screen balance
updates and `cash.changing` and `cash.changed` run before `set_cash()` returns.
Setting the current balance does nothing. It may be called from any handler,
including a cash handler; each nested update counts toward the event recursion
limit. It raises a contained error for a stale match, such as the wrapper
passed to `match.ended`.

## Base event table

Every handler receives:

```lua
event = {
    name = "event.name",
    cancelled = false
}
```

`cancelled` is shared between handlers for the same mod. It is consumed by
`lives.changing`, `tower.placing` (purchases), `tower.upgrading`, `tower.selling`,
`bloon.spawning` (round spawns), `bloon.popping`, and `bloon.leaking`.
Assigning it on another event currently has no gameplay effect.

## Event payloads

### Lifecycle and economy notifications

Match lifecycle events add `event.match`. It is valid through `match.ending`
and stale in `match.ended`, after native teardown returns.

Round lifecycle events add `event.round`. It remains valid through
`round.ended` and becomes stale after that callback returns.

### Cash events

```lua
event.cash: number  -- cash.changing, cash.changed
```

`cash` is the balance the game has just committed, read from the native
money-update event. It is the same in `cash.changing` and `cash.changed` and
is read-only; assigning to it has no effect.

### Lives events

```lua
event.old_lives: integer
event.new_lives: integer
event.player: GameObject
```

| Event | Cancellable |
| --- | --- |
| `lives.changing` | Yes |
| `lives.changed` | No |

For `lives.changing`, `new_lives` is mutable and accepts integers in
`0..2147483647`; `old_lives` is read-only. Accepted mutations are applied in
handler and profile order. Cancellation takes precedence over mutation.

### Tower events

```lua
event.tower: GameObject
```

- `tower.placing`, `tower.placed`;
- `tower.upgrading`, `tower.upgraded`; and
- `tower.selling`, `tower.sold`.

Tower event tables are read-only, while the live tower wrapper exposes
validated pop-count and sell-price accessors. `tower.placing` also carries a
read-only boolean `purchase`. It is cancellable when `purchase` is `true`;
cancelling rejects the purchase before any cost or free-tower token is
consumed, and `tower.placed` does not run. `tower.upgrading` and
`tower.selling` are cancellable. Setting `event.cancelled = true` rejects the
pending action before its first side effect and suppresses the corresponding
`tower.upgraded` or `tower.sold` event. The wrapper passed to `tower.sold`
becomes stale after all handlers return.

### Bloon events

```lua
event.bloon: GameObject
```

- `bloon.spawning`, `bloon.spawned`;
- `bloon.popping`, `bloon.popped`; and
- `bloon.leaking`, `bloon.leaked`.

Bloon event tables are read-only, while live bloon wrappers expose validated
`health()` and `set_health()` access.

`bloon.spawning` carries a read-only boolean `from_round`. It is `true` for a
bloon the round's queue is sending, and only then is the event cancellable.
Bloons a boss or a bloon ability spawns arrive with `from_round = false` and
cannot be cancelled. Cancelling a round spawn removes that bloon from the round:
it never joins the game, `bloon.spawned` does not run, and the game frees it on
its next update without any pop, reward, leak, or lives change. Its wrapper is
stale as soon as the handlers return. Child bloons released by a pop do not pass
through `bloon.spawning`.

`bloon.popping` runs at the first hit that takes a bloon's health to zero or
below, before the game decides to pop it, and is cancellable. During the handler
`health()` still reads the pre-hit value. Cancelling discards that hit: the bloon
keeps its pre-hit health (or whatever a handler set with `set_health()`), no
tower is credited with the pop, and no children or reward appear. A later lethal
hit delivers `bloon.popping` again. Further hits on a bloon that is already
popping do not repeat the event. `bloon.popped` follows when the game pops the
bloon on its next update, so other bloons' `bloon.popping` events may come in
between.

`bloon.leaking` is
cancellable. Cancellation skips that leak attempt through the native non-leak
continuation; the bloon remains valid and may attempt to leak again later.
Wrappers passed to `bloon.popped` and `bloon.leaked` become stale after all
handlers return.

### Attack and projectile events

```lua
event.attack: GameObject      -- attack.created, attack.destroyed
event.projectile: GameObject  -- projectile.created, projectile.destroyed
```

These events follow every native attack (`CWeapon`) and projectile
(`CProjectile`) created while a match is live. `*.created` runs after native
construction returns and `*.destroyed` runs as destruction begins. The
destroyed wrapper is stale after all handlers return. The wrappers expose only
`is_valid()`, `id()`, and `kind()`. Event tables are read-only and not
cancellable. Projectiles are created and destroyed many times per second in
later rounds, so keep handlers short; the host skips dispatch entirely when no
loaded mod subscribes.

## Sandbox availability

Available standard libraries:

- base;
- math;
- string;
- table; and
- UTF-8.

Unavailable facilities include `io`, `os`, `package`, `debug`, `require`,
`load`, `loadfile`, `dofile`, `collectgarbage`, native DLL loading, process
launch, arbitrary filesystem access, and networking.

Coroutines are also unavailable until the host owns their resume/close budgets.
The sandbox's `setmetatable` rejects a metatable with `__gc` or `__close` fields.
Adding `__gc` after attachment does not register a finalizer, and reattaching
that metatable is rejected. Use explicit lifecycle callbacks for cleanup.
Lifecycle functions are read directly from globals without `_G.__index` lookup.

Lua state setup, script loading, lifecycle dispatch, event construction/readback,
and timer dispatch run through protected host entry points. A boundary allocation
failure reports the mod and boundary and rejects the operation; it does not
authorize a partial event result.

Each outermost callback starts one instruction allowance and wall-clock deadline.
Callbacks nested through host re-entry share that allowance and deadline; they
cannot reset either or restart the instruction-hook countdown. Exhaustion remains
effective even if an inner error is caught. A later independent callback gets a
fresh allowance. Time checks occur at Lua instruction-hook intervals and before
and after callbacks; they cannot interrupt a currently executing native API call.

## Not yet implemented

The following are not valid v1 calls:

- gameplay-property getters and setters beyond those listed above;
- cancellation for boss or ability bloon spawns, or for towers the game adds
  without a purchase;
- live tower combat-stat mutation beyond the listed setters, arbitrary object
  spawning, and native plugins;
- general asset caching and expanded localization; and
- hot reload or runtime tower/sound registration after entry-script loading.

See the [event contract](../docs/gameplay-events.md) for timing, cancellation,
and mutation boundaries. Cross-mod cancellation is sticky: a later mod cannot
undo an earlier mod's cancellation. An invalid lives replacement retains the
last accepted value for subsequent mods.
