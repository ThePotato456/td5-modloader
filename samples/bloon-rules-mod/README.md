# Bloon Rules

[All samples](../README.md) · [Bloon contract](../../lua-api/reference.md#bloon-events)

Four independent, once-per-match examples: round-spawn cancellation, health
replacement, lethal-hit cancellation, and leak cancellation. All switches
default to `false`; logs use `[Bloon Rules]`.

## Bounded effects

| Opt-in setting | What happens once per match | What follows |
| --- | --- | --- |
| `cancel_first_spawn` | Reject one round-queue spawn | No `bloon.spawned` for that bloon |
| `boost_first_health` | Add `health_bonus` to the first observed spawn | Normal gameplay uses its new health |
| `cancel_first_pop` | Discard one lethal hit | The bloon can pop on a later hit |
| `cancel_first_leak` | Reject one leak attempt | A later retry is allowed |

Each setting defaults to `false`. Limits apply globally per match, keeping
repeated leak attempts from becoming an endless cancellation loop.

## Run it

1. Import `bloon-rules.btd5mod`, enable it in a dedicated offline profile, and
   enable one setting at a time before launching.
2. `cancel_first_spawn`: start a round. One round-queue bloon never joins the
   game and emits no `bloon.spawned`. Boss/ability spawns are not rejected.
3. `boost_first_health`: place a Dart Monkey and start a round. The first
   observed spawned bloon receives `health_bonus` extra health (default 1,
   clamped to `0..1000`); check its type and replacement health in the log.
4. `cancel_first_pop`: let a tower attack. The first lethal hit is discarded;
   that bloon keeps pre-hit health and can pop on a later hit. No children,
   reward, or tower pop credit result from the cancelled hit.
5. `cancel_first_leak`: start a match without towers. The first leak attempt is
   skipped; the same bloon may retry on the next update and is then allowed.
6. Enter another match to reset the four operation flags.

The operation limits are global per match, not per ID. A native ID can be reused
after destruction; retaining IDs forever would grow state and misclassify new
bloons. This sample keeps four booleans and schedules no per-bloon timers.

Setting health is a direct validated replacement, not a synthetic damage/pop
event. Cancelling a leak repeatedly could prevent a round from finishing; this
sample consumes that switch before returning so it rejects only one attempt.
Source: [manifest](mod.json), [Lua](lua/main.lua).
