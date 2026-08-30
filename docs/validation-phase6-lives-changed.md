# Phase 6 live lives lifecycle validation

Date: 2026-08-29

This validation covers verified pre-write and post-change Lua notifications for
lives gains and losses, including mutable proposed values and cancellation of
the pending native write.

## Binary research and symbol validation

Read-only analysis of the fingerprinted Steam Win32 4.8 executable identified
two native observer handlers that write the same stored lives field:

- `player.lives.gain.handler` at RVA `0x32C490`, reached from
  `CGiveLivesEvent`; and
- `player.lives.loss.handler` at RVA `0x32CAE0`, reached from
  `CBloonEscapedEvent`.

The same analysis identified the exact accepted mutation instructions:

- `player.lives.gain.write` at RVA `0x32C4E2`; and
- `player.lives.loss.write` at RVA `0x32CDF6`.

The gain handler adds the event amount unless the active mode suppresses lives
gains. The escaped-bloon handler conditionally subtracts the escaped bloon's
damage and clamps the result at zero. Both instructions occur only after those
rules accept the update. The symbol inspector resolved all four signatures
uniquely. A missing symbol or failed hook restores any instruction already
patched, rolls back the hook transaction, and prevents Lua mods from loading.

## Hook contract and automated fixture

The runtime validates the original six bytes before replacing each write with a
five-byte x86 jump and padding byte. The shim preserves registers and flags,
dispatches `lives.changing` while the stored value is still unchanged, executes
the original add/subtract instruction when Lua leaves the proposal unchanged,
writes an accepted replacement absolute value when Lua mutates `new_lives`, or
skips the write when Lua cancels the event. Every path resumes at the following
native instruction.
For a loss, `new_lives` reflects the game's subsequent zero clamp.

The existing handler hooks snapshot the value before the complete native
handler, invoke it, and read the value afterward. They dispatch `lives.changed`
only when both reads succeed and the value differs. Both event tables contain
integer `old_lives` and `new_lives` fields.

The x86 fixtures verify pre-write ordering, proposed gain and clamped-loss
values, replacement gain and loss writes, cancelled gain and loss writes,
original instruction behavior, exact byte restoration, post-change detection,
zero-value suppression,
unavailable-state containment, and clean removal. Dispatching from the handler
entry remains deliberately avoided because game-mode rules can still reject an
attempted update there.

## Interactive copied-game acceptance

The strengthened Release smoke workflow launched the ignored copied game
through Steam with the lifecycle sample enabled. In an ordinary offline
single-player match, a bloon was deliberately allowed to escape. Lua observed
`lives.changing 200->199` and then `lives.changed 200->199`, in that order, at
`2026-08-29T10:10:41.460Z`.

A second Release smoke run enabled the sample's opt-in cancellation setting.
During an ordinary match, leaked bloons still emitted `bloon.leaked`, while Lua
repeatedly received and cancelled `lives.changing 200->199`. The runtime logged
`lives.changing:cancelled`; subsequent events still reported 200 as the stored
old value, and no matching `lives.changed 200->199` appeared during the harness
settling period. The first verified cancellation occurred at
`2026-08-29T10:20:26.764Z`.

A third Release smoke run enabled the sample's opt-in mutation setting. Lua
received the proposed loss `1->0`, replaced `new_lives` with `2`, and then
received `lives.changed 1->2` from the completed native handler. The ordered
evidence was recorded at `2026-08-30T17:38:54.235Z` and
`2026-08-30T17:38:54.236Z`. The dedicated `--expect-lives-mutation` verifier
reported `LIVE_SMOKE_PASS` and closed its exact game process.

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
`lives.changing.new_lives` accepts Lua integers from `0` through `2147483647`.
`old_lives` is observational and ignored if reassigned. Mutations are applied in
handler and profile order; invalid types or ranges retain the last accepted
value. Cancellation takes precedence and does not reverse the originating
reward or bloon leak. Interactive loss-path replacement is now accepted; an
ordinary in-game gain originating from the game itself remains fixture-only.
This validation does not claim an interactive native gain test, an on-screen
overlay, or online safety enforcement.
