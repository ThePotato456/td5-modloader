# Gameplay event contract v1

This document defines the Phase 6 Lua event and object-lifetime foundation.
The contract is implemented and mock-host tested; individual events become
live only after their fingerprinted game hooks pass the Phase 6 acceptance
gate.

Currently live in the supported Steam Win32 4.8 build:

- `match.starting`, immediately before `CGameScreen::Init`;
- `match.started`, after `CGameScreen::Init` returns;
- `match.ending`, immediately before `CGameScreen::Uninit`; and
- `match.ended`, after `CGameScreen::Uninit` returns.
- `round.starting`, immediately before native `CRoundStartedEvent` dispatch;
- `round.started`, after native `CRoundStartedEvent` dispatch;
- `round.ending`, immediately before native `CRoundEndedEvent` dispatch; and
- `round.ended`, after native `CRoundEndedEvent` dispatch;
- `cash.changing`, immediately before native `CMoneyUpdatedEvent` observer
  dispatch; and
- `cash.changed`, after native `CMoneyUpdatedEvent` observer dispatch; and
- `lives.changing`, immediately before a verified native gain or loss writes the
  stored lives value;
- `lives.changed`, after that native handler completes and the stored value is
  verified to have changed;
- `tower.placing`, immediately before the tower manager takes ownership;
- `tower.placed`, after native `CTowerSpawnedEvent` observer dispatch;
- `tower.upgrading`, after native eligibility succeeds and before mutation;
- `tower.upgraded`, after native `CTowerUpgradedEvent` observer dispatch;
- `tower.selling`, after native sale eligibility succeeds and before sale side
  effects begin; and
- `tower.sold`, after native `CTowerSoldEvent` observer dispatch;
- `bloon.spawning`, immediately before the bloon manager takes ownership;
- `bloon.spawned`, after native `CBloonSpawnedEvent` observer dispatch;
- `bloon.popping`, after native pop acceptance and before pop side effects;
- `bloon.popped`, after native `CBloonPoppedEvent` observer dispatch; and
- `bloon.leaking`, after the track-end comparison succeeds and before leak side
  effects;
- `bloon.leaked`, after native `CBloonEscapedEvent` observer dispatch.

Lives events carry `old_lives` and `new_lives`; cash events currently carry no
value fields. Each
live tower notification carries `event.tower`, and each live bloon notification
carries `event.bloon`. `lives.changing.new_lives` is mutable;
`lives.changing`, `tower.upgrading`, `tower.selling`, and `bloon.leaking` are
cancellable. Setting
`event.cancelled = true` skips the pending lives write or routes an accepted
tower action into the game's own rejection path. Other live events cannot cancel
or modify an action. The
game has already updated its internal balance when
`cash.changing` runs; the pre/post distinction describes the native observer
dispatch boundary. `lives.changing` runs at the exact add/subtract instruction,
after the game has accepted the update, and `new_lives` reflects the clamped
result for losses. Lua may replace `new_lives` with an integer in
`0..2147483647`. Mutations flow through handlers and then mods in deterministic
profile order; invalid types and ranges are rejected. Cancellation takes
precedence over mutation. Cancellation affects only the lives delta: it does not undo
the originating reward or bloon leak. When cancelled, `lives.changed` does not
run because the stored value remains unchanged. Cancelling `tower.upgrading`
occurs before the first upgrade mutation, so `tower.upgraded` does not run.
Cancelling `tower.selling` occurs before removal and refund side effects, so
`tower.sold` does not run. `tower.placing` remains a non-cancellable read-only
pre-event. The `tower.upgrading` and `tower.selling` payloads remain read-only.
Cancelling `bloon.leaking` skips only that accepted leak attempt through the
native non-leak continuation; the live bloon can reach the boundary again on a
later update. `bloon.spawning` and `bloon.popping` remain non-cancellable, and
all bloon payloads remain read-only.

## Subscription

```lua
local token = btd5.events.on("tower.placed", function(event)
    btd5.log("info", "tower " .. event.tower:id() .. " was placed")
end)

btd5.events.off(token)
```

`on` accepts only a v1 event name and returns a mod-local integer token. `off`
returns `true` once and `false` for unknown or already removed tokens. Each mod
may keep at most 256 active handlers.

Dispatch is deterministic: enabled mods run in resolved profile order, then
handlers run in subscription order. A handler subscribed during dispatch does
not observe that in-progress event. Unsubscribing a handler takes effect before
its next invocation, including during an in-progress dispatch.

A handler error is logged with the mod ID, event name, and token. Only that
handler is disabled; later handlers and other Lua states continue. Event
dispatch nesting is limited to eight levels per mod to contain mutation-driven
feedback loops.

## Event names

| Area | Pre-events | Post-events |
| --- | --- | --- |
| Match | `match.starting` (live), `match.ending` (live) | `match.started` (live), `match.ended` (live) |
| Round | `round.starting` (live), `round.ending` (live) | `round.started` (live), `round.ended` (live) |
| Economy | `cash.changing` (live), `lives.changing` (live) | `cash.changed` (live), `lives.changed` (live) |
| Tower placement | `tower.placing` (live) | `tower.placed` (live) |
| Tower upgrade | `tower.upgrading` (live) | `tower.upgraded` (live) |
| Tower sale | `tower.selling` (live) | `tower.sold` (live) |
| Bloon spawn | `bloon.spawning` (live) | `bloon.spawned` (live) |
| Bloon pop | `bloon.popping` (live) | `bloon.popped` (live) |
| Bloon leak | `bloon.leaking` (live) | `bloon.leaked` (live) |

Pre-event cancellation and field mutation are represented by a shared event
table, so earlier handler changes are visible to later handlers. A hook may
honor `event.cancelled = true` only when that event is documented as
cancellable. `lives.changing` currently honors cancellation. Its
`old_lives` is read-only. `new_lives` accepts integers in
`0..2147483647`, and an accepted replacement changes the pending native write.
Other event fields remain read-only until their validators and exact commit
boundaries are implemented.

## Object wrappers

The v1 object kinds are `match`, `round`, `player`, `tower`, `attack`,
`projectile`, and `bloon`. Lua receives opaque userdata rather than an address.
Every wrapper contains a host-issued object ID, generation, scene epoch, and
kind. The native registry resolves all four values before allowing access.

```lua
if event.tower:is_valid() then
    btd5.log("info", event.tower:kind() .. ":" .. event.tower:id())
end
```

Destroying an object invalidates its generation. Starting a new scene
invalidates every wrapper from the previous scene. IDs may be reused, but a new
generation prevents an old wrapper from resolving to the replacement object.
Calling `id()` or `kind()` on a stale wrapper raises a contained Lua error.

Tower wrappers additionally expose `pop_count()` and
`set_pop_count(value)`. The setter accepts integers in `0..2147483647`, applies
the value immediately through the fingerprinted game's verified virtual
setter, and rejects stale wrappers, other object kinds, invalid types or ranges,
and builds where the accessor symbols do not resolve uniquely.

For live tower notifications, `tower.placing` creates the tower's stable handle
before manager ownership and `tower.placed` reuses it after the native spawned
event. Upgrades retain that identity, and sale invalidates the handle after all
`tower.sold` handlers return. The sold wrapper is valid during the callback so a
handler can identify it, then `is_valid()` returns `false`. Match teardown also
invalidates every remaining tower handle before `match.ended` is dispatched.

For live bloon notifications, spawn creates a stable handle. The same handle is
used when that native bloon later pops or leaks. A popped parent and its spawned
child layers are distinct objects with distinct handles. Popped and leaked
wrappers remain valid during their callbacks, then become stale after all
handlers return. Match teardown invalidates any remaining bloon handles.

Additional game-specific getters and validated setters will be added only with
their corresponding verified native accessors. Raw addresses are never part of
the public Lua API.
