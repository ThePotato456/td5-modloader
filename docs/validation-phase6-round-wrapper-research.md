# Phase 6 round-wrapper research

Date: 2026-09-11

This record establishes the native identity used by `event.round`.

## Verified event construction

Read-only inspection of the fingerprinted Steam Win32 4.8 executable found the
only direct construction references to both supported round-event vtables.
Each allocation contains the vtable followed by two copied scalar fields:

- `CRoundStartedEvent` is constructed near RVA `0x228B90` from source offsets
  `+0x14` and `+0x1C`;
- `CRoundEndedEvent` is constructed near RVA `0x323FEF` from the same source
  offsets; and
- each allocation is immediately passed to `event.manager.dispatch` at RVA
  `0x5AC020`.

The event manager may destroy the allocation during that call. This is why the
existing hook captures supported payload data before dispatch and never reads
the event afterward.

## Owner correlation

Further call-site tracing correlated both producers. The round-end update loads
the state object from `[[game + 0xB8] + 0x7C]`, copies its `+0x14` and `+0x1C`
fields into `CRoundEndedEvent`, then calls the round-start producer at RVA
`0x228A00` with that exact state pointer as its receiver. The producer updates
the same fields and emits `CRoundStartedEvent`. Its three direct call sites use
the same receiver contract.

The symbol map resolves the producer uniquely as `round.lifecycle.start`. A
transactional detour captures the receiver before the producer emits
`round.starting`. The registry exposes that handle through all four events,
invalidates it after `round.ended`, and creates a new generation at the next
start. This preserves logical round lifetimes even though BTD5 reuses the native
allocation. Scene teardown remains the fallback invalidation boundary when a
match exits during an active round.

The native hook fixture verifies receiver capture before the original call and
clean removal. The supported-build inspector resolves RVA `0x228A00` uniquely.

## Interactive copied-game acceptance

The Release smoke workflow launched the ignored copied game through Steam with
the lifecycle sample and round-wrapper assertions enabled. During one ordinary
offline round, the runtime and Lua sample observed the four boundaries in order:

- `round.starting` at `2026-09-11T21:58:17.821Z`;
- `round.started` at `2026-09-11T21:58:17.834Z`;
- `round.ending` at `2026-09-11T21:58:24.573Z`; and
- `round.ended` at `2026-09-11T21:58:24.573Z`.

Each Lua observation is emitted after checking that `event.round` is valid, has
kind `round`, and retains the same wrapper identity through the matching end
notification. The run contained no Lua or runtime error. Registry and dispatch
fixtures separately verify that the wrapper becomes stale after `round.ended`;
the bounded live harness exits as soon as the final callback succeeds, before a
later timer can record the post-callback stale check.
