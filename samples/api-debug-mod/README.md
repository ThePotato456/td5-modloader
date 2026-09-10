# API Debug Console

A bounded development mod for observing and demonstrating the currently
implemented Lua API. Its default configuration is read-only and emits compact
round summaries instead of logging every high-frequency event. It also
exercises localization and packaged-resource access, runs one deterministic
timer, and persists session counters in mod-owned storage.

Install `api-debug-console.btd5mod`, enable its newest version in a profile,
launch BTD5, and watch the manager's live Runtime Log. Every record from this
package starts with `[API Debug]`. Package versions are immutable: if an older
debug package is already installed, install this newer version and select it in
the profile instead of copying over the existing archive. Packaged resource
files belong under `assets/`; this sample reads `assets/about.txt`.

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
| `log_event_details` | `false` | Log a bounded sample of each implemented gameplay event. |
| `detailed_event_limit` | `2` | Maximum detailed records per event type; clamped to `0..20`. |
| `log_round_summaries` | `true` | Emit one compact tower/bloon counter summary per round. |
| `log_object_properties` | `false` | Include live properties in sampled event records. |
| `verify_terminal_wrappers` | `false` | Schedule bounded stale-wrapper checks after terminal events. |
| `terminal_wrapper_limit` | `3` | Maximum stale-wrapper timers; clamped to `0..20`. |
| `operation_limit` | `1` | Maximum uses of each enabled mutation or cancellation; clamped to `0..20`. |
| `mutate_tower_pop_count` | `false` | Replace a newly placed tower's pop count. |
| `tower_pop_count` | `123` | Replacement used by the pop-count switch. |
| `mutate_tower_sell_price` | `false` | Replace every newly placed tower's base sell price. This setting is not capped by `operation_limit`. |
| `tower_sell_price` | `777` | Replacement used by the sell-price switch. |
| `mutate_bloon_health` | `false` | Add the configured bonus to a bounded number of newly spawned bloons. |
| `bloon_health_bonus` | `1.0` | Health added by the bloon-health switch. |
| `replace_lives_below_minimum` | `false` | Clamp accepted losses to `minimum_lives`. |
| `minimum_lives` | `1` | Replacement floor for the lives-mutation example. |
| `cancel_lives_losses` | `false` | Cancel a bounded number of verified life losses. |
| `cancel_tower_upgrades` | `false` | Reject a bounded number of upgrades before their first side effect. |
| `cancel_tower_sales` | `false` | Reject a bounded number of sales before removal and refund side effects. |
| `cancel_bloon_leaks` | `false` | Reject a bounded number of leak attempts. |

`cancel_lives_losses` takes precedence over `replace_lives_below_minimum`.
Every gameplay-changing feature is independently limited by `operation_limit`.
In particular, bloon-leak cancellation is globally bounded so a bloon cannot be
kept alive in a repeated leak loop by this sample. Avoid enabling every mutation
at once when diagnosing one feature; small, isolated scenarios produce clearer
logs.

Versions before `1.1.0` logged every event and scheduled a stale-wrapper timer
for every terminal bloon event. That behavior could overwhelm the render thread
in later rounds. Do not use those versions; install and select `1.1.0` or newer.

This package is a developer diagnostic, not a gameplay-balanced mod. Use it in
Steam Offline Mode on the supported copied Steam Win32 4.8 build.
