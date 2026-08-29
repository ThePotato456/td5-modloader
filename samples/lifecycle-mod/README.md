# Lifecycle Sample

This publishable sample uses only the sandboxed Lua API. It logs each lifecycle
stage, persists a launch counter in mod-owned storage, reads configuration and
localization, schedules a deterministic timer, and logs all live match and
round lifecycle events plus cash, lives, and tower placing/placed/upgraded/sold
notifications. The tower handlers demonstrate stable wrapper identity and
pre/post placement ordering as well as post-sale invalidation. Bloon handlers log spawned, popped, and leaked
identities and verify post-pop and post-leak invalidation.
Lives handlers verify matching `old_lives` and `new_lives` values across the
exact pre-write and post-change notifications.

The `cancel_lives_loss` setting defaults to `false`. When explicitly enabled,
the sample cancels life-loss writes from `lives.changing` while leaving the
originating bloon leak intact. It is intended for the dedicated cancellation
smoke test and is not enabled during ordinary sample use.
