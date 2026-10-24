# Tower Rules

[All samples](../README.md) · [Tower events](../../docs/gameplay-events.md)

Demonstrates purchase, upgrade, and sale cancellation, type/tier reads, live
pop-count and base sale-value setters, and a stale-wrapper check. All switches
default to off. Logs use `[Tower Rules]`.

## Action rules

| Action | Opt-in switch | First eligible attempt per match | Retry |
| --- | --- | --- | --- |
| Purchase | `block_first_purchase` | Reject before cash/free-token consumption | Allow |
| Upgrade | `block_first_upgrade` | Reject before upgrade mutation | Allow |
| Sale | `block_first_sale` | Reject before removal/refund | Allow |

All switches default to `false`. With `override_properties=true`, a new tower
also receives the configured pop count and base sale value. The game's normal
sale multiplier still applies.

## Run it

1. Import `tower-rules.btd5mod` and enable it in an offline test profile.
2. Turn on `block_first_purchase`, launch, and try to buy a tower. The first
   purchase is rejected before its cost/free token is consumed; retry succeeds.
3. With `block_first_upgrade` or `block_first_sale`, the first corresponding
   action in each match is rejected. Retry to see normal behavior.
4. With `override_properties`, a newly placed tower receives `pop_count` (default
   123) and `sell_price` (default 777). Both are clamped to `0..2147483647`.
5. Sell a tower and inspect the timer's `stale=true` log. Enter another match
   to reset the once-per-action cancellation flags.

The sale setter changes the **base** value used by the game's payout routine;
the ordinary refund multiplier still applies. It does not promise a payout of
777. These methods do not change attack damage, firing speed, or upgrades.

The three `block_first_*` settings each default to `false` and reject at most
one eligible action per match, globally across towers. `override_properties`
also defaults to `false`; when enabled it applies to every newly placed tower.

This example has one deferred check per successful sale, never per projectile.
It captures the ID before destruction and only calls `is_valid()` afterward.
Source: [manifest](mod.json), [Lua](lua/main.lua).
