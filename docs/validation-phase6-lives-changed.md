# Phase 6 live lives-changed validation

Date: 2026-08-29

This validation covers a post-change Lua notification for verified lives gains
and losses. It does not make `lives.changing` live and does not expose or mutate
the lives value.

## Binary research and symbol validation

Read-only analysis of the fingerprinted Steam Win32 4.8 executable identified
two native observer handlers that write the same stored lives field:

- `player.lives.gain.handler` at RVA `0x32C490`, reached from
  `CGiveLivesEvent`; and
- `player.lives.loss.handler` at RVA `0x32CAE0`, reached from
  `CBloonEscapedEvent`.

The gain handler adds the event amount unless the active mode suppresses lives
gains. The escaped-bloon handler conditionally subtracts the escaped bloon's
damage and clamps the result at zero. The symbol inspector resolved both
signatures uniquely. Either missing symbol or either failed detour rolls back
the hook transaction and prevents Lua mods from loading.

## Hook contract and automated fixture

The runtime snapshots the stored lives value immediately before a recognized
gain or loss handler, invokes the original handler, and reads it again. It
dispatches `lives.changed` only when both reads succeed and the values differ.
The old and new values remain internal until the public payload schema and
validation rules are implemented.

The x86 fixture verifies gain and loss detection, old/new comparison, zero-value
suppression, unavailable-state containment, original mutation behavior, and
clean removal of both detours.

`lives.changing` is intentionally not dispatched. At handler entry, game-mode
rules may still reject the requested update; emitting a pre-event there would
create false change notifications. A verified pre-mutation boundary remains a
separate Phase 6 item.

## Interactive copied-game acceptance

The Release smoke workflow launched the ignored copied game through Steam with
the lifecycle sample enabled. An ordinary offline single-player match and round
were started, then a bloon was deliberately allowed to escape. After the game
reduced its stored lives value, the runtime and Lua sample each recorded exactly
one `lives.changed` notification at `2026-08-29T09:18:36.324Z`.

The harness reported `LIVE_SMOKE_PASS`, closed only its exact process, and left
no BTD5 process running. The copied game retained its supported hashes:

| File | SHA-256 |
| --- | --- |
| `BTD5-Win.exe` | `BDC4F4AEC679F51B8763FF7FE517A2556E392D99576045ECE117FCAFDDA27B70` |
| `Assets/BTD5.jet` | `906AA89D690C27664CE47A1A2E3EAC756D7CF551FE3E1669EC22AE814346B9A8` |

## Scope

The live acceptance proves the loss path in an ordinary match. The gain path is
installed through the second fingerprinted handler and is covered by the native
fixture, but this record does not claim an interactive in-game gain test.
`lives.changed` currently carries only the event name and cannot cancel or
modify a change. This validation does not claim `lives.changing`, bloon payloads,
an on-screen overlay, or online safety enforcement.
