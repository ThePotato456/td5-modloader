# FIXUP Phase 5 — Vanilla launch isolation

Date: 2026-09-05 (America/Chicago). Tested revision: `59bda5a` plus the
uncommitted Phase 2–5 changes delivered with this record. Final Steam acceptance
ran at 04:02 UTC on 2026-09-06, still September 5 locally. Its Debug runtime
SHA-256 was `CF3327BA0FA92ABDD81AA0D0B2FBADA69317A786CFF56AA253BD6DC6E0F0C06D`.

## Launch contract and review

The manager starts the selected game, observes a newly created process at the
expected executable path, and writes a ticket addressed to its PID and exact
Windows process creation time. The ticket also binds the game installation,
profile, build, data root, issue time, and expiry. Its lifetime is at most 60
seconds. No timestamp-only or environment-based authorization remains.

The runtime waits up to 10 seconds for the ticket naming its own process. An
atomic `CREATE_NEW` claim prevents simultaneous readers and replay. Wrong
process/creation time, installation, mode, malformed fields, future issuance,
expired tickets, and excessive lifetimes are rejected. PID reuse cannot reuse
an earlier process's ticket. The manager waits for an acknowledgment from that
same PID before reporting a successful modded launch, then removes its ticket,
claim, and acknowledgment files.

Steam launches use the registered app manifest and `steam.exe -applaunch 306020`.
They pass no private game arguments and require no inherited manager environment.
Disposable copies launch directly with the development Steam App ID environment;
their authorization still comes only from the process-bound ticket. The manager
rejects an already-running game and holds a cross-process file lock throughout
each launch request for the same installation. A competing manager request
cannot replace its profile or change its mode while it awaits acknowledgment.

Vanilla launch creates no ticket. The runtime ignores the old shared profile,
`BTD5ML_ACTIVE_PROFILE`, and a stale inherited `BTD5ML_DATA_ROOT`; no Lua states
or game hooks are installed. Vanilla logs use the normal local application-data
root. The manager validates the actual Steam installation, its supported game
hashes, and either absence of a loader or verified loader ownership/content.
It additionally requires the `BTD5Loader_LaunchProtocol1` export, inspected as
PE metadata without loading the DLL. An old runtime matching an old record or
bundle is therefore insufficient evidence of isolation.

Mode changes never rename or temporarily remove loader DLLs. Failure and
cancellation clean up the current request's ticket. If the manager crashes
before cleanup, a leftover ticket cannot authorize another process. Foreign or
modified loader files are preserved and block launch with a repair/update/remove
message. General game crash recovery remains ACTIONPLAN Phase 9 work.

## Automated evidence

- Debug and Release suites passed all three CTest entries and the managed
  integration suite. Native and managed static analysis and formatting passed.
- Native tests exercise expiry, future issuance, malformed time types, wrong
  process identity/creation time, wrong installation and mode, replay, concurrent
  claims, and a subsequent valid request.
- The real proxy/runtime fixture ignores a fresh legacy shared profile and old
  environment variables before compatibility checking or hook installation. A
  ticket for the fixture's actual process reaches compatibility checking.
- Managed tests cover Steam manifest discovery, copied folders without a
  manifest, invalid installation paths, PE capability detection, ticket cleanup,
  already-running games, competing launches, Windows process-start failures,
  cancellation while awaiting acknowledgment, and successful retry.
- A modified proxy blocks vanilla launch and remains byte-for-byte preserved.
  These managed process tests use an injected launcher; they do not substitute
  for the Steam acceptance below.

## Registered-Steam acceptance

`BTD5ModLoader.LiveSmoke ... --expect-vanilla-isolation` used the registered
Steam copy with the loader installed, not a copied executable or URI-only mock.
The harness verified ownership before updating the installed loader and used
the dedicated `FIXUP Vanilla Acceptance` profile. The final run demonstrated:

1. Modded launch through Steam, sample load/readiness/timer, and normal shutdown
   at approximately 04:02:00–04:02:01 UTC.
2. A valid leftover ticket addressed to that exited process, simulating
   interrupted manager cleanup.
3. Vanilla launch within the former 60-second fallback window, a
   `vanilla_no_ticket` record at 04:02:13 UTC, no Lua or game-hook records,
   and normal window close. The orphan ticket was not consumed.
4. Another modded launch, sample readiness/timer, and normal shutdown at
   approximately 04:02:23–04:02:24 UTC.
5. Unchanged executable and asset-archive hashes throughout. The harness removed
   its simulated orphan; logs and account-specific output remain local only.

| File | SHA-256 |
| --- | --- |
| `BTD5-Win.exe` | `BDC4F4AEC679F51B8763FF7FE517A2556E392D99576045ECE117FCAFDDA27B70` |
| `Assets/BTD5.jet` | `906AA89D690C27664CE47A1A2E3EAC756D7CF551FE3E1669EC22AE814346B9A8` |

Earlier attempts are not counted as passing acceptance. The game rejected an
experimental private launch argument; process-bound tickets replaced it. A later
attempt exposed an inherited logging directory, which vanilla now ignores.
The final sequence above used the corrected implementation and required no
forced termination. It proves launch isolation, not complete-match gameplay
coverage or network enforcement.

Phase 5 is complete. FIXUP Phase 6 byte-bounded log reading is next.
