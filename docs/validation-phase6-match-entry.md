# Phase 6 live match-entry validation

Date: 2026-08-28

This validation covers the first live gameplay-event boundary. Match exit and
round lifecycle events remain open Phase 6 work.

This is the historical entry-only result from 2026-08-28. Match teardown was
implemented and accepted later in the
[complete match lifecycle validation](validation-phase6-match-lifecycle.md).

## Symbol and hook validation

The read-only symbol inspector resolved `screen.game.init` uniquely in the
fingerprinted Steam Win32 4.8 executable and validated its expected function
prologue. The symbol is required: an unresolved address or failed detour blocks
Lua mod loading.

The runtime installs the hook through the existing transaction after the render
hook. The x86 test fixture verifies the adapted member-function bridge invokes
callbacks in this order:

1. `match.starting` callback;
2. original game-screen initialization; and
3. `match.started` callback.

Removing the hook restores a direct original call with no callbacks.

## Interactive copied-game acceptance

The Release interactive smoke workflow launched the ignored copied game with
the lifecycle sample enabled. After an ordinary offline single-player match was
selected, the runtime log recorded:

- native `events:match.starting`;
- Lua `Lifecycle Sample observed match.starting`;
- native `events:match.started`; and
- Lua `Lifecycle Sample observed match.started`.

The original initialization completed between the two events in approximately
157 milliseconds. The smoke harness then closed only its exact test process;
no BTD5 process remained.

The game executable and asset archive retained their supported hashes:

| File | SHA-256 |
| --- | --- |
| `BTD5-Win.exe` | `BDC4F4AEC679F51B8763FF7FE517A2556E392D99576045ECE117FCAFDDA27B70` |
| `Assets/BTD5.jet` | `906AA89D690C27664CE47A1A2E3EAC756D7CF551FE3E1669EC22AE814346B9A8` |

## Scope

These events currently carry only the event name. No match object or mutable
field is exposed until a verified teardown boundary can invalidate the wrapper.
The validation does not claim match exit, round events, gameplay mutation, or
on-screen rendering.
