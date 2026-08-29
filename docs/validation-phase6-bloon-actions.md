# Phase 6 live bloon-action validation

Date: 2026-08-29

This validation covers read-only post-action Lua notifications and
lifetime-checked wrappers for native bloon spawning, popping, and leaking. It
does not make bloon pre-events live or expose native addresses.

## Binary research and symbol validation

Read-only analysis of the fingerprinted Steam Win32 4.8 executable correlated
retained RTTI, complete-object locators, vtables, event construction sites, and
native event-manager dispatches. The symbol inspector resolved all targets
uniquely:

- `event.manager.dispatch` at RVA `0x5AC020`;
- `event.bloon.spawned.vtable` at RVA `0x7CE970`;
- `event.bloon.popped.vtable` at RVA `0x7CE958`; and
- `event.bloon.escaped.vtable` at RVA `0x7CE920`.

Each native event carries the affected bloon pointer. Disassembly of the pop
path confirmed the popped parent remains alive through observer dispatch, then
child layers are created as separately spawned native objects. The leak path
likewise retains the escaped object through observer dispatch before cleanup. A
missing signature or failed detour rolls back the complete gameplay-event hook
transaction and prevents Lua mods from loading.

## Hook contract and automated coverage

The shared event hook classifies each event by its verified vtable before native
dispatch. After native observers return, it emits:

- `bloon.spawned` for `CBloonSpawnedEvent`;
- `bloon.popped` for `CBloonPoppedEvent`; and
- `bloon.leaked` for `CBloonEscapedEvent`.

Before dispatch, the hook captures the bloon pointer and maps it to a
generation- and scene-checked opaque handle. Spawn retains the handle; pop and
leak invalidate it after every mod handler returns. It never reads the native
event after dispatch. The x86 fixtures cover payload capture, post-only ordering,
stable handle reuse, stale-handle rejection, original dispatch behavior,
unrelated-event filtering, exception containment, and clean hook removal.

## Interactive copied-game acceptance

The strengthened Release smoke workflow launched the ignored copied game
through Steam with the lifecycle sample enabled. In an ordinary offline
single-player match, a round produced native spawn, pop, and leak events with
stable identities:

- spawned ID `2` at `2026-08-29T09:59:11.481Z`, then popped ID `2` at
  `2026-08-29T09:59:24.202Z`;
- spawned ID `18` at `2026-08-29T09:59:16.161Z`, then leaked ID `18` at
  `2026-08-29T09:59:44.572Z`; and
- saved popped and leaked wrappers both reported stale on the next rendered
  frame after their respective callback.

Each native event had a matching lifecycle-sample Lua observation. The harness
reported `LIVE_SMOKE_PASS`, closed only its exact process, and left no BTD5
process running. The copied game retained its supported hashes:

| File | SHA-256 |
| --- | --- |
| `BTD5-Win.exe` | `BDC4F4AEC679F51B8763FF7FE517A2556E392D99576045ECE117FCAFDDA27B70` |
| `Assets/BTD5.jet` | `906AA89D690C27664CE47A1A2E3EAC756D7CF551FE3E1669EC22AE814346B9A8` |

## Scope

These wrappers support only `is_valid()`, `id()`, and `kind()`. They expose no
native address, gameplay properties, or mutation. `bloon.spawning`,
`bloon.popping`, and `bloon.leaking` remain pending until true pre-action
boundaries and validation rules are verified. This result does not claim custom
content, an on-screen overlay, or online safety enforcement.
