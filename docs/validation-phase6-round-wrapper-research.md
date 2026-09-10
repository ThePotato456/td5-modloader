# Phase 6 round-wrapper research

Date: 2026-09-10

This record explains why the live round lifecycle currently has notifications
but does not expose `event.round`.

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

## Lifetime decision

The two scalar payload fields do not establish a durable native round-object
identity. The producer receivers also come from different code paths and have
not been proven to refer to the same owning object. Registering either event
allocation would therefore make the wrapper unsafe after the pre-event, while
registering a producer receiver would claim an identity that the binary evidence
does not support.

`event.round` remains open until a stable owner can be correlated across both
the start and end producers, including its destruction or reuse boundary. The
runtime continues to expose the four verified round notifications without an
object payload.
