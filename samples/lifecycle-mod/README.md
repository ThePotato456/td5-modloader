# Lifecycle Sample

This publishable sample uses only the sandboxed Lua API. It logs each lifecycle
stage, persists a launch counter in mod-owned storage, reads configuration and
localization, schedules a deterministic timer, and logs all live match and
round lifecycle events plus cash, lives, and tower
placing/placed/upgrading/upgraded/selling/sold notifications. The tower handlers
demonstrate stable wrapper identity and pre/post action ordering as well as
post-sale invalidation. Bloon handlers log pre/post spawn, pop, and leak
identities, verify action ordering, and verify post-pop and post-leak
invalidation.
Lives handlers verify matching `old_lives` and `new_lives` values across the
exact pre-write and post-change notifications.

The `cancel_lives_loss` setting defaults to `false`. When explicitly enabled,
the sample cancels life-loss writes from `lives.changing` while leaving the
originating bloon leak intact. It is intended for the dedicated cancellation
smoke test and is not enabled during ordinary sample use.

The `mutate_lives_loss` setting also defaults to `false`. The dedicated
mutation smoke test enables it to replace a proposed life loss with a one-life
gain. The resulting `lives.changed` event must contain the replacement value,
which proves the Lua mutation reached the native commit boundary. Do not enable
this test-only behavior during ordinary sample use.

The `mutate_tower_pop_count` setting defaults to `false`. Its dedicated live
smoke test reads a newly placed tower's pop count, changes it to `123` through
the validated wrapper setter, reads it back, and logs the transition.

The `cancel_tower_actions` setting defaults to `false`. Its dedicated smoke
test cancels accepted upgrade and sale attempts before their first side effect.
The tower remains placed and valid after both rejected actions.

The `mutate_direct_properties` setting defaults to `false`. Its dedicated smoke
test changes a placed tower's sell price to `777` and adds one health to a
spawned bloon, then reads both values back through their wrappers.
