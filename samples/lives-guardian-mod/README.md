# Lives Guardian

A focused mutation example using the one gameplay cancellation path currently
supported by the live runtime.

When enabled, a loss that would reduce the player below `minimum_lives` is
cancelled at the exact native lives-write boundary. The originating bloon still
leaks; only the lives write is skipped. Gains and losses that remain at or above
the configured minimum are unchanged.

Configuration:

- `enabled`: set to `false` to disable protection;
- `minimum_lives`: non-negative integer floor, default `1`.

This example requests `gameplay.mutate` because it changes the result of a live
game action.
