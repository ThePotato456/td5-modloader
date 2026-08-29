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
- `lives.changed`, after a verified native gain or loss changed the stored lives
  value;
- `tower.placed`, after native `CTowerSpawnedEvent` observer dispatch;
- `tower.upgraded`, after native `CTowerUpgradedEvent` observer dispatch; and
- `tower.sold`, after native `CTowerSoldEvent` observer dispatch;
- `bloon.spawned`, after native `CBloonSpawnedEvent` observer dispatch;
- `bloon.popped`, after native `CBloonPoppedEvent` observer dispatch; and
- `bloon.leaked`, after native `CBloonEscapedEvent` observer dispatch.

`lives.changing`, tower pre-events, and bloon pre-events remain mock-host only.
Cash and lives events currently carry no value fields. Each live tower
notification carries `event.tower`, and each live bloon notification carries
`event.bloon`. No fields are mutable and no live event can cancel or modify an
action. The game has already updated its internal balance when
`cash.changing` runs; the pre/post distinction describes the native observer
dispatch boundary. `lives.changed` is deliberately post-only until a
pre-mutation boundary can distinguish a real change from a mode-suppressed
attempt. Tower notifications are deliberately post-only because the native
spawned, upgraded, and sold events occur after their actions are committed.
Bloon notifications are likewise post-only because the native events occur
after spawning, popping, or leaking has been committed.

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
| Economy | `cash.changing` (live), `lives.changing` | `cash.changed` (live), `lives.changed` (live) |
| Tower placement | `tower.placing` | `tower.placed` (live) |
| Tower upgrade | `tower.upgrading` | `tower.upgraded` (live) |
| Tower sale | `tower.selling` | `tower.sold` (live) |
| Bloon spawn | `bloon.spawning` | `bloon.spawned` (live) |
| Bloon pop | `bloon.popping` | `bloon.popped` (live) |
| Bloon leak | `bloon.leaking` | `bloon.leaked` (live) |

Pre-event cancellation and field mutation are represented by a shared event
table, so earlier handler changes are visible to later handlers. A hook may
honor `event.cancelled = true` only when that event is documented as
cancellable. Field schemas, ranges, and live mutability remain unimplemented;
game hooks must not consume arbitrary table changes until those validators are
added.

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

For live tower notifications, placement creates or reuses the tower's stable
handle, upgrades retain that identity, and sale invalidates the handle after all
`tower.sold` handlers return. The sold wrapper is valid during the callback so a
handler can identify it, then `is_valid()` returns `false`. Match teardown also
invalidates every remaining tower handle before `match.ended` is dispatched.

For live bloon notifications, spawn creates a stable handle. The same handle is
used when that native bloon later pops or leaks. A popped parent and its spawned
child layers are distinct objects with distinct handles. Popped and leaked
wrappers remain valid during their callbacks, then become stale after all
handlers return. Match teardown invalidates any remaining bloon handles.

Game-specific getters and validated setters will be added with the corresponding
live hooks. Raw addresses are never part of the public Lua API.
