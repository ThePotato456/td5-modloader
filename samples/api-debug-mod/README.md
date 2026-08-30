# API Debug Console

A comprehensive development mod for observing and demonstrating the currently
implemented Lua API. Its default configuration is read-only: it logs lifecycle
and gameplay events, inspects live object properties, verifies terminal wrapper
invalidation, exercises localization and packaged-resource access, runs a
deterministic timer, and persists session counters in mod-owned storage.

Install `api-debug-console.btd5mod`, enable its newest version in a profile,
launch BTD5, and watch the manager's live Runtime Log. Every record from this
package starts with `[API Debug]`. Package versions are immutable: if an older
debug package is already installed, install this newer version and select it in
the profile instead of copying over the existing archive.

## Demonstrated APIs

- `on_load`, `on_ready`, and `on_shutdown`;
- `btd5.log`, `btd5.config`, `btd5.storage`, `btd5.localization`,
  `btd5.resource`, and `btd5.timer`;
- subscription and unsubscription for all 24 gameplay events;
- stable tower and bloon IDs, kinds, lifetimes, and stale-wrapper checks;
- tower pop-count and sell-price getters/setters;
- bloon-health getters/setters;
- lives payload replacement; and
- supported lives, tower-action, and bloon-leak cancellation.

## Configuration

All gameplay-changing switches default to `false`.

| Setting | Default | Effect |
| --- | ---: | --- |
| `log_every_event` | `true` | Log every implemented gameplay event. |
| `log_object_properties` | `true` | Include live tower or bloon properties in event records. |
| `mutate_tower_pop_count` | `false` | Replace a newly placed tower's pop count. |
| `tower_pop_count` | `123` | Replacement used by the pop-count switch. |
| `mutate_tower_sell_price` | `false` | Replace a newly placed tower's base sell price. |
| `tower_sell_price` | `777` | Replacement used by the sell-price switch. |
| `mutate_bloon_health` | `false` | Add the configured bonus to each newly spawned bloon. |
| `bloon_health_bonus` | `1.0` | Health added by the bloon-health switch. |
| `replace_lives_below_minimum` | `false` | Clamp accepted losses to `minimum_lives`. |
| `minimum_lives` | `1` | Replacement floor for the lives-mutation example. |
| `cancel_lives_losses` | `false` | Cancel every verified loss of lives. |
| `cancel_tower_upgrades` | `false` | Reject upgrades before their first side effect. |
| `cancel_tower_sales` | `false` | Reject sales before removal and refund side effects. |
| `cancel_bloon_leaks` | `false` | Reject each accepted leak attempt. |

`cancel_lives_losses` takes precedence over `replace_lives_below_minimum`.
Cancelled bloon leaks can be attempted again by the same live bloon on later
updates. Avoid enabling every mutation at once when diagnosing one feature;
small, isolated scenarios produce clearer logs.

This package is a developer diagnostic, not a gameplay-balanced mod. Use it in
Steam Offline Mode on the supported copied Steam Win32 4.8 build.
