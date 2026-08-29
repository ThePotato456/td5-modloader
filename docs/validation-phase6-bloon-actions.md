# Phase 6 live bloon-action validation

Date: 2026-08-29

This validation covers read-only post-action Lua notifications for native bloon
spawning, popping, and leaking. It does not make bloon pre-events live or expose
a bloon wrapper.

## Binary research and symbol validation

Read-only analysis of the fingerprinted Steam Win32 4.8 executable correlated
retained RTTI, complete-object locators, vtables, event construction sites, and
native event-manager dispatches. The symbol inspector resolved all targets
uniquely:

- `event.manager.dispatch` at RVA `0x5AC020`;
- `event.bloon.spawned.vtable` at RVA `0x7CE970`;
- `event.bloon.popped.vtable` at RVA `0x7CE958`; and
- `event.bloon.escaped.vtable` at RVA `0x7CE920`.

Each native event carries the affected bloon pointer. The public events remain
payload-free because a pop may change a bloon layer without destroying the
underlying object, while leak and full-pop cleanup use different lifetime paths.
A missing signature or failed detour rolls back the complete gameplay-event hook
transaction and prevents Lua mods from loading.

## Hook contract and automated coverage

The shared event hook classifies each event by its verified vtable before native
dispatch. After native observers return, it emits:

- `bloon.spawned` for `CBloonSpawnedEvent`;
- `bloon.popped` for `CBloonPoppedEvent`; and
- `bloon.leaked` for `CBloonEscapedEvent`.

It never reads the native event after dispatch. The existing x86 fixture covers
post-only ordering, original dispatch behavior, unrelated-event filtering,
exception containment, and clean hook removal. The Lua event-catalog fixture
confirms all three public names remain accepted.

## Interactive copied-game acceptance

The Release smoke workflow launched the ignored copied game through Steam with
the lifecycle sample enabled. In an ordinary offline single-player match, a
round produced native spawn, pop, and leak events. The first observation of each
kind was:

- `bloon.spawned` at `2026-08-29T09:51:37.405Z`;
- `bloon.popped` at `2026-08-29T09:51:57.253Z`; and
- `bloon.leaked` at `2026-08-29T09:52:40.880Z`.

Each native event had a matching lifecycle-sample Lua observation. The harness
reported `LIVE_SMOKE_PASS`, closed only its exact process, and left no BTD5
process running. The copied game retained its supported hashes:

| File | SHA-256 |
| --- | --- |
| `BTD5-Win.exe` | `BDC4F4AEC679F51B8763FF7FE517A2556E392D99576045ECE117FCAFDDA27B70` |
| `Assets/BTD5.jet` | `906AA89D690C27664CE47A1A2E3EAC756D7CF551FE3E1669EC22AE814346B9A8` |

## Scope

These notifications carry only the event name. They cannot identify or mutate a
bloon, cancel an action, or distinguish a layer pop from final destruction.
`bloon.spawning`, `bloon.popping`, and `bloon.leaking` remain pending until true
pre-action boundaries and lifetime rules are verified. This result does not
claim custom content, an on-screen overlay, or online safety enforcement.
