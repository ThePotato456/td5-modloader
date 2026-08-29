# Phase 6 live tower-action validation

Date: 2026-08-29

This validation covers post-action Lua notifications and lifetime-checked tower
wrappers for an ordinary placement, upgrade, and sale. It does not make tower
pre-events live or expose native addresses.

## Binary research and symbol validation

Read-only analysis of the fingerprinted Steam Win32 4.8 executable correlated
retained RTTI, event construction sites, tower pointers, and native event-manager
calls. The symbol inspector resolved all required targets uniquely:

- `event.manager.dispatch` at RVA `0x5AC020`;
- `event.tower.spawned.vtable` at RVA `0x7D2774`;
- `event.tower.upgraded.vtable` at RVA `0x7D2794`; and
- `event.tower.sold.vtable` at RVA `0x7F1C5C`.

Each event object contains the affected tower pointer. The spawn event is
constructed after placement, the upgrade event after the upgrade is applied,
and the sold event during the completed sale path. A missing signature or
failed detour rolls back the complete gameplay-event hook transaction and
prevents Lua mods from loading.

## Hook contract and automated fixture

The shared native event hook supports bindings with a before callback, an after
callback, or both. The three tower bindings use only an after callback and emit:

- `tower.placed` after native `CTowerSpawnedEvent` observer dispatch;
- `tower.upgraded` after native `CTowerUpgradedEvent` observer dispatch; and
- `tower.sold` after native `CTowerSoldEvent` observer dispatch.

The hook classifies the event and captures its tower pointer before native
dispatch, then never reads the event object afterward because an observer may
destroy it. The runtime maps the pointer to a generation- and scene-checked
opaque handle. Placement creates the handle, upgrades reuse it, and sale
invalidates it after every mod handler returns. Match teardown invalidates any
remaining handles. The x86 fixtures verify pre-dispatch payload capture,
post-only ordering, stable handle reuse, stale-handle rejection, original
dispatch behavior, unrelated-event filtering, and clean hook removal.

## Interactive copied-game acceptance

The strengthened Release smoke workflow launched the ignored copied game
through Steam with the lifecycle sample enabled. In an ordinary offline
single-player match, one tower was placed, upgraded once, and sold. The runtime
and Lua sample recorded the same opaque ID for all three actions:

- `tower.placed`, ID `1`, at `2026-08-29T09:43:00.156Z`;
- `tower.upgraded`, ID `1`, at `2026-08-29T09:43:01.612Z`; and
- `tower.sold`, ID `1`, at `2026-08-29T09:43:05.748Z`.

On the next rendered frame at `2026-08-29T09:43:05.764Z`, the Lua sample
confirmed that the saved sold-tower wrapper's `is_valid()` result was `false`.

The harness reported `LIVE_SMOKE_PASS`, closed only its exact process, and left
no BTD5 process running. The copied game retained its supported hashes:

| File | SHA-256 |
| --- | --- |
| `BTD5-Win.exe` | `BDC4F4AEC679F51B8763FF7FE517A2556E392D99576045ECE117FCAFDDA27B70` |
| `Assets/BTD5.jet` | `906AA89D690C27664CE47A1A2E3EAC756D7CF551FE3E1669EC22AE814346B9A8` |

## Scope

These are read-only post-action notifications. The wrapper supports only
`is_valid()`, `id()`, and `kind()`; it exposes no native address, gameplay
properties, or mutation. `tower.placing`, `tower.upgrading`, and `tower.selling`
remain pending until true pre-action boundaries and validation rules exist. This
result does not claim custom-tower registration, bloon events, an on-screen
overlay, or online safety enforcement.
