# Phase 6 lifecycle-hook foundation validation

Date: 2026-08-28

This validation covers the first Phase 6 increment. It does not complete the
core gameplay object/event gate.

## Hook design

The runtime installs a MinHook detour over the imported Win32
`GDI32!SwapBuffers` function. The exact supported executable was independently
confirmed to import that function. This avoids guessing a game-object calling
convention and does not patch the executable on disk.

The hook is installed through the existing transaction mechanism. A creation
or enable failure prevents Lua mods from loading; shutdown and later startup
failures remove the hook. Re-entrant frame dispatch is ignored so a callback
cannot recursively advance the lifecycle scheduler.

On the first rendered frame after a profile finishes loading, the runtime:

1. transitions from `ModsLoading` to `GameReady` exactly once;
2. invokes each enabled mod's `on_ready()` callback in profile order; and
3. advances each isolated Lua timer scheduler once per rendered frame.

## Copied-game acceptance

The rerunnable Release smoke workflow safely removed only the loader artifacts
recorded by its isolated manager state, installed the newly staged artifacts,
reinstalled the lifecycle sample package, launched the ignored copied game, and
waited for all of these log records:

- `hooks_ready=render.swap_buffers`;
- `Hello from Lua (launch 2)`;
- `sample.lifecycle:loaded`;
- `game_ready_frame_hook`;
- `Lifecycle Sample is ready`; and
- `deterministic timer fired` after the sample's 60-frame delay.

The exact test process was then closed. No BTD5 process remained. The copied
proprietary files retained their supported-build hashes:

| File | SHA-256 |
| --- | --- |
| `BTD5-Win.exe` | `BDC4F4AEC679F51B8763FF7FE517A2556E392D99576045ECE117FCAFDDA27B70` |
| `Assets/BTD5.jet` | `906AA89D690C27664CE47A1A2E3EAC756D7CF551FE3E1669EC22AE814346B9A8` |

The Release build, native tests, proxy fixture, manager integration tests,
formatting checks, MSVC analysis, and managed analyzers also passed.

## Remaining Phase 6 work

Render-frame readiness is a lifecycle foundation, not a live gameplay API. The
mock host now validates the v1 event bus and generation/scene-checked wrappers
for matches, rounds, players, towers, attacks, projectiles, and bloons. The
match and round lifecycles are validated separately; economy/object
hooks, object properties, cancellation, and validated mutation remain
unimplemented. Timer ticks currently represent rendered frames, not verified
simulation updates. No on-screen overlay or gameplay mutation is claimed by
this validation.
