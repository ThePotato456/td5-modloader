# Phase 6 live match lifecycle validation

Date: 2026-08-29

This validation extends the live match-entry boundary through game-screen
teardown. Round lifecycle events remain open Phase 6 work.

## Symbol and hook validation

The read-only inspector resolved both required symbols uniquely in the
fingerprinted Steam Win32 4.8 executable:

- `screen.game.init` at RVA `0x31A6C0`; and
- `screen.game.uninit` at RVA `0x31EC20`.

The teardown address was correlated from the adjacent `Init`/`Uninit` virtual
slots declared by the researched screen hierarchy, then independently matched
to a unique function signature in the supported executable. The runtime owns
both detours as one operation: either both hooks enable, or installation rolls
back and Lua mods do not load.

The x86 fixture verifies this order:

1. `match.starting` callback;
2. original `CGameScreen::Init`;
3. `match.started` callback;
4. `match.ending` callback;
5. original `CGameScreen::Uninit`; and
6. `match.ended` callback.

Removing the hook restores direct calls to both original functions with no Lua
callbacks.

## Interactive copied-game acceptance

The Release smoke workflow launched the ignored copied game through Steam with
the lifecycle sample enabled. An ordinary offline single-player match was
entered and then left through the in-game pause/quit flow. The runtime log
recorded the native event and matching Lua observation for all four boundaries,
in order:

- `match.starting` at `2026-08-29T08:23:34.684Z`;
- `match.started` at `2026-08-29T08:23:34.835Z`;
- `match.ending` at `2026-08-29T08:23:53.511Z`; and
- `match.ended` at `2026-08-29T08:23:53.524Z`.

The smoke harness reported `LIVE_SMOKE_PASS`, then closed only its exact test
process. No BTD5 process remained.

The copied game files retained their supported hashes:

| File | SHA-256 |
| --- | --- |
| `BTD5-Win.exe` | `BDC4F4AEC679F51B8763FF7FE517A2556E392D99576045ECE117FCAFDDA27B70` |
| `Assets/BTD5.jet` | `906AA89D690C27664CE47A1A2E3EAC756D7CF551FE3E1669EC22AE814346B9A8` |

## Scope

Match lifecycle events currently carry only the event name. This validation
does not claim round events, gameplay mutation, a live match object payload,
on-screen rendering, or completion of an entire played match.
