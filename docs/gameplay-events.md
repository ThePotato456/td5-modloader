# Gameplay event contract v1

[Documentation home](README.md) · [API signatures](../lua-api/reference.md) ·
[Focused examples](../samples/README.md)

This is the current Lua API 1 event and object-lifetime contract. All 28 events
below are implemented for the verified Steam Win32 4.8 build. See
[native internals](native-internals.md) for the underlying object layouts and
native dispatch boundaries.

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
- `tower.placing`, at the player's purchase decision before any free-tower
  token or cost is consumed (`purchase = true`), or, for towers the game adds
  without a purchase, immediately before the tower manager takes ownership
  (`purchase = false`);
- `tower.placed`, after native `CTowerSpawnedEvent` observer dispatch;
- `tower.upgrading`, after native eligibility succeeds and before mutation;
- `tower.upgraded`, after native `CTowerUpgradedEvent` observer dispatch;
- `tower.selling`, after native sale eligibility succeeds and before sale side
  effects begin; and
- `tower.sold`, after native `CTowerSoldEvent` observer dispatch;
- `bloon.spawning`, immediately before the bloon manager takes ownership
  (`from_round = true` for the round's own spawns);
- `bloon.spawned`, after native `CBloonSpawnedEvent` observer dispatch;
- `bloon.popping`, at the first lethal hit, before the pop is decided;
- `bloon.popped`, after native `CBloonPoppedEvent` observer dispatch; and
- `bloon.leaking`, after the track-end comparison succeeds and before leak side
  effects;
- `bloon.leaked`, after native `CBloonEscapedEvent` observer dispatch;
- `attack.created`, after a native attack (`CWeapon`) constructor returns;
- `attack.destroyed`, as native attack destruction begins;
- `projectile.created`, after a native `CProjectile` constructor returns; and
- `projectile.destroyed`, as native projectile destruction begins.

Match events carry `match`. It remains valid from `match.starting` through
`match.ending`; the same wrapper is stale in `match.ended` because native
teardown has returned. Lives events carry `player`, backed by the native state
object used by the verified lives read and write boundaries. It remains valid
only within the current match scene epoch.

Round events carry `round`, backed by the native state receiver captured before
the round-start producer emits its event. The wrapper remains valid through
`round.ended` and becomes stale immediately after that callback. BTD5 reuses
the native allocation, but the registry assigns the next logical round a new
generation.

Attack and projectile lifetime events carry `attack` or `projectile`. They
follow the base native objects (`CWeapon` and `CProjectile`) through their
only constructors and destructor, which were confirmed against the
fingerprinted executable's vtable references. `*.created` runs after native
construction returns; `*.destroyed` runs as native destruction of the base
object begins. Only objects constructed while a match scene is live are
tracked, so factory prototypes built during asset loading never reach Lua. The
destroyed wrapper is valid during its callbacks and stale afterwards; match
teardown invalidates any remaining handle. These events are high volume: the
host dispatches them only when a mod subscribes, never logs them individually,
and none are cancellable or mutable. An attack is not linked to its owning
tower, and projectiles expose identity methods only.

Lives events also carry `old_lives` and `new_lives`. Cash events carry a
read-only number `cash`: the balance the game has just written, copied from
the native `CMoneyUpdatedEvent` before dispatch. Both `cash.changing` and
`cash.changed` report the same committed value. A nested money update raised
by a native observer reports its own balance. Each
live tower notification carries `event.tower`, and each live bloon notification
carries `event.bloon`. `lives.changing.new_lives` is mutable;
`lives.changing`, `tower.placing` (purchases), `tower.upgrading`, `tower.selling`,
`bloon.spawning` (round spawns), `bloon.popping`, and `bloon.leaking` are
cancellable. Setting
`event.cancelled = true` skips the pending lives write or routes an accepted
tower action into the game's own rejection path. Other live events cannot cancel
or modify an action. The
game has already updated its internal balance when
`cash.changing` runs; the pre/post distinction describes the native observer
dispatch boundary. `lives.changing` runs at the exact add/subtract instruction,
after the game has accepted the update. It is not delivered when that write
leaves the stored value unchanged, such as a loss at zero lives or a zero-point
gain. `new_lives` reflects the clamped
result for losses. Lua may replace `new_lives` with an integer in
`0..2147483647`. Mutations flow through handlers and then mods in deterministic
profile order; invalid types and ranges are rejected. Cancellation takes
precedence over mutation. Cancellation affects only the lives delta: it does not undo
the originating reward or bloon leak. When cancelled, `lives.changed` does not
run because the stored value remains unchanged. Cancelling `tower.upgrading`
occurs before the first upgrade mutation, so `tower.upgraded` does not run.
Cancelling `tower.selling` occurs before removal and refund side effects, so
`tower.sold` does not run. `tower.placing` is cancellable only when
`event.purchase` is `true`: cancellation skips the free-token and cost checks
and routes the attempt into the game's own unaffordable-placement rejection, so
no cash or token is spent, the tower is not placed, and `tower.placed` does not
run. The player can try again, which delivers a new `tower.placing`. A
purchase that proceeds delivers `tower.placing` once; the later manager
ownership boundary does not repeat it. The `tower.upgrading` and `tower.selling` payloads remain read-only.
Cancelling `bloon.leaking` skips only that accepted leak attempt through the
native non-leak continuation; the live bloon can reach the boundary again on a
later update. Cancelling `bloon.popping` discards that lethal hit: the bloon
keeps its pre-hit health, no tower is credited with the pop, and a later hit can
deliver `bloon.popping` again. Cancelling `bloon.spawning` is honoured only when
`event.from_round` is `true`: the bloon never joins the manager, `bloon.spawned`
does not run, and it is queued for the game's own deferred deletion, so the
round simply has one bloon fewer. Boss and ability spawns are not cancellable.
All bloon payloads remain read-only.

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
| Attack lifetime | none | `attack.created` (live), `attack.destroyed` (live) |
| Projectile lifetime | none | `projectile.created` (live), `projectile.destroyed` (live) |

Pre-event cancellation and field mutation are represented by a table shared
among handlers within one mod. Each mod has its own Lua state and event table;
accepted mutations flow to later mods in resolved order. Cancellation is sticky
across mods, so a later mod cannot undo an earlier mod's cancellation. A hook may
honor `event.cancelled = true` only when that event is documented as
cancellable. The complete supported list is given above, including the
purchase/round-spawn conditions. For `lives.changing`, `old_lives`
is read-only and `new_lives` accepts integers in
`0..2147483647`, and an accepted replacement changes the pending native write.
Other event fields remain read-only until their validators and exact commit
boundaries are implemented.

After callbacks return, the host reads payload fields directly without invoking
Lua metatable methods. Deleted or incorrectly typed scalar fields retain their
original host values and report a validation error. Numeric payload validation
rejects numeric strings, NaN, and infinities. A missing or non-boolean
`cancelled` value does not cancel the action.

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
`set_pop_count(value)`, plus `sell_price()` and `set_sell_price(value)`. Integer
setters accept `0..2147483647`. Pop count uses the game's verified virtual
accessors; sell price uses a fingerprinted field consumed by the verified sale
payout routine.

`tower:type()` returns a known vanilla name or custom `modId:towerId`, or `nil`
for an unnamed type. `tower:upgrade_tiers()` returns left and right path tiers
in `0..4`. `bloon:type()` returns the named type without camo/regrow modifiers.
`round:number()` returns the played round number. These are read-only identity
properties. Match wrappers expose `match:set_cash(value)` for finite values in
`0..2147483647`; cash events dispatch synchronously before that setter returns.
See the [signature reference](../lua-api/reference.md#gameobject) for full ranges
and errors. Player, attack, and projectile wrappers have identity/lifetime
methods only.

For live tower notifications, `tower.placing` creates the tower's stable handle
before manager ownership and `tower.placed` reuses it after the native spawned
event. Upgrades retain that identity, and sale invalidates the handle after all
`tower.sold` handlers return. The sold wrapper is valid during the callback so a
handler can identify it, then `is_valid()` returns `false`. Match teardown also
invalidates every remaining tower handle before `match.ended` is dispatched.

Every tracked tower and bloon handle is also invalidated when the game's own
`CBaseTower` or `CBloon` destructor begins, so no wrapper can resolve to a freed
object on a path that bypasses sale, pop, leak, or scene teardown. Destructor
invalidation does not dispatch an additional Lua event.

For live bloon notifications, spawn creates a stable handle. The same handle is
used when that native bloon later pops or leaks. A popped parent and its spawned
child layers are distinct objects with distinct handles. Popped and leaked
wrappers remain valid during their callbacks, then become stale after all
handlers return. Match teardown invalidates any remaining bloon handles.
Bloon wrappers expose `health()` and `set_health(value)`. Health must be finite,
nonnegative, and representable as a 32-bit float. The fingerprint is anchored
to the native damage commit that reads and writes the same field. Direct health
replacement does not itself synthesize a damage or pop event.

Additional game-specific getters and validated setters will be added only with
their corresponding verified native accessors. Raw addresses are never part of
the public Lua API.
