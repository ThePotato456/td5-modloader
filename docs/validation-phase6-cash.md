# Phase 6 live cash-event validation

Date: 2026-08-29

This validation covers read-only Lua notification of native cash updates. It
does not add a balance payload, cash mutation, or lives events.

This is the historical cash-only result. A verified post-change lives
notification was implemented and accepted later in the
[lives validation](validation-phase6-lives-changed.md).

## Binary research and symbol validation

The supported Steam Win32 4.8 executable retains RTTI for
`CMoneyUpdatedEvent`. Read-only correlation of its complete-object locator,
vtable, construction sites, and native observer-dispatch calls identified the
additional required symbol. The symbol inspector resolved the gameplay-event
targets uniquely:

- `event.manager.dispatch` at RVA `0x5AC020`;
- `event.round.started.vtable` at RVA `0x7D1C04`;
- `event.round.ended.vtable` at RVA `0x7E6380`; and
- `event.money.updated.vtable` at RVA `0x7D758C`.

An unresolved symbol or failed detour rolls back the complete gameplay-event
hook transaction and prevents Lua mods from loading.

## Hook contract and automated fixture

The reusable native event hook classifies an event from its vtable before
native dispatch and never reads it afterward because native observers may
destroy it. A recognized money event invokes callbacks in this order:

1. `cash.changing`;
2. original native `CMoneyUpdatedEvent` observer dispatch; and
3. `cash.changed`.

The x86 fixture verifies round and money event classification, unrelated-event
filtering, callback order, the original return value and queued-state argument,
post-dispatch safety, and clean hook removal.

## Interactive copied-game acceptance

The Release smoke workflow launched the ignored copied game through Steam with
the lifecycle sample enabled. Its strengthened cash mode required two complete
Lua cash-event pairs so the automatic match-load notification could not satisfy
the test alone.

The first pair occurred during match initialization, before `match.started`:

- `cash.changing` and `cash.changed` at `2026-08-29T09:03:50.655Z`.

After match entry, placing a tower produced a second pair:

- `cash.changing` and `cash.changed` at `2026-08-29T09:03:54.472Z`.

Each native record had a matching lifecycle-sample Lua observation. The harness
reported `LIVE_SMOKE_PASS`, closed only its exact process, and left no BTD5
process running. The copied game retained its supported hashes:

| File | SHA-256 |
| --- | --- |
| `BTD5-Win.exe` | `BDC4F4AEC679F51B8763FF7FE517A2556E392D99576045ECE117FCAFDDA27B70` |
| `Assets/BTD5.jet` | `906AA89D690C27664CE47A1A2E3EAC756D7CF551FE3E1669EC22AE814346B9A8` |

## Scope

These names describe the boundary around native money-update observer dispatch.
The game updates its internal balance before constructing and dispatching
`CMoneyUpdatedEvent`, so `cash.changing` is not currently a cancellable or
mutable before-balance-change event. Both callbacks carry only the event name.
This validation does not claim old/new balance fields, mutation, cancellation,
lives coverage, an on-screen overlay, or online safety enforcement.
