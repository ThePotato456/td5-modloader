# FIXUP Phase 7 — Gameplay capability coverage

Reviewed 2026-09-06 against `56f675a` plus the production coordinator extraction
and tests delivered with this record. Historical live records below prove their
stated scenarios on the supported build; this review does not claim a new live
run of the extracted coordinator.

## Coverage matrix

All event handlers run in registration order within each mod and in profile order
across mods. Only the four listed pre-events consume cancellation. Accepted lives
replacements propagate to later mods; cancellation is sticky across mods and takes
precedence at the native write. Tower/bloon event fields are observational; wrapper
setters are separate synchronous native operations.

| Contract / events | Production native boundary | Automated evidence | Recorded live evidence / remaining work |
| --- | --- | --- | --- |
| `match.starting`, `match.started`, `match.ending`, `match.ended`; notifications | `MatchHook`, runtime match registration; match end invalidates objects | `foundation_tests.cpp` match hook and object registry fixtures | [Match lifecycle](validation-phase6-match-lifecycle.md), [entry](validation-phase6-match-entry.md); [50-round teardown/re-entry accepted](validation-phase6-complete-match.md); full-match mutation coverage remains open |
| `round.starting`, `round.started`, `round.ending`, `round.ended`; notifications | `NativeEventHook` round vtable before/after observers | Native event filtering/order fixture | [Round lifecycle](validation-phase6-round-lifecycle.md) |
| `cash.changing`, `cash.changed`; notifications, no numeric payload/setter or cancellation | `NativeEventHook` cash observer | Native event filtering/order fixture | [Cash](validation-phase6-cash.md) |
| `lives.changing`, `lives.changed`; `old_lives` observational; `new_lives` integer replacement only on changing, range 0–2147483647; changing cancellable | `LivesWriteHook` gain/loss write and `LivesHook` post-write observer | Gain/loss hook fixtures; Lua raw/type checks; new `mod_dispatch_tests.cpp` profile propagation, rejected range, sticky cancellation | [Lives](validation-phase6-lives-changed.md): loss cancellation/replacement accepted; native gain remains fixture-only |
| `tower.placing`, `tower.placed`; notifications with tower handle | `TowerPlacementHook`, native placed observer | Placement boundary and Lua object fixtures | [Tower actions](validation-phase6-tower-actions.md); placing cancellation unsupported |
| `tower.upgrading`, `tower.upgraded`; pre-event cancellable | `TowerUpgradeHook` rejection branch, native upgraded observer | Upgrade hook cancellation/restoration; new production profile cancellation tests | [Tower actions](validation-phase6-tower-actions.md): upgrade rejection accepted |
| `tower.selling`, `tower.sold`; pre-event cancellable; sold handle invalidated after dispatch | `TowerSaleHook` rejection branch, native sold observer | Sale hook cancellation/restoration; registry stale handles; new production profile cancellation tests | [Tower actions](validation-phase6-tower-actions.md): sale rejection and saved sold-handle invalidation accepted |
| `bloon.spawning`, `bloon.spawned`, `bloon.popping`, `bloon.popped`; notifications; popped handle invalidated after dispatch | `BloonActionHook` and native spawned/popped observers | Lua bloon identity/removal and native observer fixtures | [Bloon actions](validation-phase6-bloon-actions.md); spawning/popping cancellation unsupported |
| `bloon.leaking`, `bloon.leaked`; pre-event cancellable without invalidating handle; leaked handle invalidated after dispatch | `BloonActionHook` non-leak continuation, native leaked observer | Leak cancellation/restoration/fallthrough fixture; new production profile cancellation tests | [Bloon actions](validation-phase6-bloon-actions.md): notifications and interactive cancellation accepted (2026-09-06) |
| Tower `pop_count()` / `set_pop_count(integer)`; 0–2147483647 | Resolved tower getter/setter functions | Lua type/range/kind/stale-object fixtures | [Properties](validation-phase6-tower-properties.md): immediate live readback accepted |
| Tower `sell_price()` / `set_sell_price(integer)`; 0–2147483647 | Fingerprinted sale-field access | Lua property validation fixture | [Properties](validation-phase6-tower-properties.md): immediate live readback accepted; not final payout/persistence proof |
| Bloon `health()` / `set_health(number)`; finite nonnegative float-representable value | Fingerprinted health-field access | Lua property validation fixture | [Properties](validation-phase6-tower-properties.md): immediate live readback accepted; setting zero does not itself dispatch pop events |
| Handle identity, kind, validity; no native address exposed | `GameObjectRegistry`, runtime tower/bloon dispatch and invalidation | Registry identity/generation and Lua stale/kind fixtures | Tower/bloon records above; other live wrapper integrations remain open |
| Stop new dispatch, drain admitted callbacks, reverse shutdown order | `ModDispatchGate`, `shutdown_mods`, normal-exit hook | `exit_hook_tests.cpp`, real proxy fixture, new coordinator traversal/shutdown test | [FIXUP shutdown](validation-fixup-phase4.md): two normal-exit runs |

## Production coordination tests

The runtime now calls `dispatch_profile_lives` and `dispatch_profile_event` from
`mod_dispatch.hpp`. Tests use those same functions with real, separate Lua states,
not a second implementation of the profile loop. They assert that later mods see
the last accepted lives replacement after an invalid value, cannot undo another
mod's cancellation, and still run after cancellation. Object cancellation covers
upgrade, sale, and leak names. Gate admission and reverse shutdown exercise the
same shared lifecycle helpers used by runtime shutdown; the existing exit/proxy
fixtures cover the actual shutdown boundary and export.

## Claims deliberately left open

Generic match/round/player/attack/projectile kind tags and event-name subscription
tests do not prove live wrappers for those kinds. Full-match mutation acceptance still
requires duplicate-hook checks, teardown and stale-object scenarios, and
a native life gain. Bloon leak cancellation passed on 2026-09-06. ACTIONPLAN Phase 6 remains open;
this FIXUP phase repairs coverage and claims without completing those features.

## Verification

Debug and Release builds passed all three CTest entries (native suite, isolated
Lua boundaries, and real proxy fixture), plus manager core integration tests.
Native and managed static analysis passed, including the rebuilt production
runtime. Formatting and `git diff --check` passed. The extracted coordinator
preserves the existing loop behavior; no new live gameplay acceptance is claimed.

The [2026-09-06 victory run](validation-phase6-complete-match.md) subsequently accepted
50-round ordering, stale handles at scene teardown, and second-match entry.
