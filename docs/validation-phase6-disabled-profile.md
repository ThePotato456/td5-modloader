# Phase 6 disabled-profile validation

Date: 2026-08-30

This acceptance check verifies the Phase 6 off-switch contract: an installed
sample package that remains in a profile but is disabled must not load or run on
the next modded launch.

## Repeatable scenario

The managed live-smoke harness supports `--expect-disabled`. It installs the
Lifecycle Sample, enables it long enough to verify inherited configuration,
disables the profile entry, writes a fresh runtime handoff, and launches the
ignored copied Steam build. The verifier waits for runtime initialization to
settle and rejects any sample load or callback evidence.

The generated handoff contained an empty enabled-mod list:

```json
{
  "schemaVersion": 1,
  "profile": "Live Smoke",
  "buildId": "steam-win32-4.8",
  "mods": []
}
```

## Copied-game acceptance

The copied Steam Win32 4.8 game produced these runtime milestones:

- supported-build detection at `2026-08-30T18:14:05.922Z`;
- all required hooks ready at `2026-08-30T18:14:06.533Z`;
- active-mod loading complete at `2026-08-30T18:14:06.534Z`; and
- the game-ready render boundary at `2026-08-30T18:14:06.920Z`.

After a two-second settle window, the runtime log contained none of the
following:

- `sample.lifecycle:loaded`;
- the configured `Hello from Lua` greeting;
- any `Lifecycle Sample` callback, timer, mutation, or event record.

The harness reported `LIVE_SMOKE_PASS` and closed only the game process it had
started. This proves that disabling the profile entry excludes the package from
the very next handoff and prevents its Lua state from loading. The native loader
still initializes because this is a modded launch with an empty profile; it
does not claim that the loader itself is absent.
