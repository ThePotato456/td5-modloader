# Phase 6 live round lifecycle validation

Date: 2026-08-29

This validation covers the first complete live round-event cycle. It does not
add mutable event fields or a live round object payload.

## Binary research and symbol validation

The supported Steam Win32 4.8 executable retains RTTI for
`CRoundStartedEvent` and `CRoundEndedEvent`. Read-only correlation of their
complete-object locators, vtables, construction sites, and native observer
dispatch calls identified three required symbols. The symbol inspector resolved
each uniquely:

- `event.manager.dispatch` at RVA `0x5AC020`;
- `event.round.started.vtable` at RVA `0x7D1C04`; and
- `event.round.ended.vtable` at RVA `0x7E6380`.

An unresolved symbol or failed detour rolls back the hook transaction and
prevents Lua mods from loading.

## Hook contract and automated fixture

The hook classifies the event from its vtable before native dispatch. It never
reads the event afterward because the game may destroy it during dispatch. For
the two recognized event types, callbacks run in this order:

1. `round.starting` or `round.ending`;
2. original native observer dispatch; and
3. `round.started` or `round.ended`.

The x86 fixture verifies both event types, the two-argument member calling
convention, the original return value, queued-state forwarding, unrelated-event
filtering, post-dispatch safety, and clean hook removal.

## Interactive copied-game acceptance

The Release smoke workflow launched the ignored copied game through Steam with
the lifecycle sample enabled. One ordinary offline single-player round was
started and completed. The runtime recorded exactly one native event and one
matching Lua observation for each boundary, in order:

- `round.starting` at `2026-08-29T08:51:46.124Z`;
- `round.started` at `2026-08-29T08:51:46.144Z`;
- `round.ending` at `2026-08-29T08:52:06.901Z`; and
- `round.ended` at `2026-08-29T08:52:06.901Z`.

The harness reported `LIVE_SMOKE_PASS`, closed only its exact process, and left
no BTD5 process running. The copied game retained its supported hashes:

| File | SHA-256 |
| --- | --- |
| `BTD5-Win.exe` | `BDC4F4AEC679F51B8763FF7FE517A2556E392D99576045ECE117FCAFDDA27B70` |
| `Assets/BTD5.jet` | `906AA89D690C27664CE47A1A2E3EAC756D7CF551FE3E1669EC22AE814346B9A8` |

## Scope

These boundaries wrap the game’s native round-event observer dispatch. They
carry only the event name and are not cancellable or mutable. This validation
does not claim round-number fields, a live round object, economy mutation,
on-screen rendering, or an entire match played to victory/defeat.
