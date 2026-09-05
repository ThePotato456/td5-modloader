# ACTIONPLAN alignment audit

Audited on 2026-09-05 against commit `1d0bb85`. The existing uncommitted
`scripts/offline-game.ps1` edits were inspected as local tooling, not treated as
completed runtime network enforcement. Product code and tracker checkboxes were
not changed by this audit.

The implementation broadly follows the planned architecture, but the completed
Phase 4 and Phase 5 claims need correction before further gameplay features.
Passing current tests does not establish all the guarantees stated in the plan.

## Evidence and limits

- Debug build, native suite, proxy fixture, and manager integration suite passed
  during this audit. The Release suite, static analysis, and formatting passed
  earlier in this same session on the code committed as `1d0bb85`.
- The read-only symbol inspector accepted the copied Steam Win32 4.8 build and
  resolved all 31 current symbols. It did not launch or modify the game.
- Isolated x86 probes compiled the current Lua host and reproduced both the
  lifecycle metatable execution and nested-callback budget failures below.
- Reviewed all phase-specific acceptance documents. Their game and WPF
  walkthroughs are historical evidence, not newly repeated manual acceptance.
- Checked tracked filenames for game executables, DLLs, asset archives, mod
  packages, local configuration, and ignored build/data directories; none were
  found. A targeted tracked-text scan found no user-home paths or 17-digit
  identifiers. This is a sanity scan, not a comprehensive secrets/provenance audit.
- This was not a fresh checkout build, a complete-match playthrough, a release
  certification, or exhaustive verification of every native destruction path.

## Phase-by-phase alignment

| Phase | Assessment | Implementation and evidence |
| --- | --- | --- |
| Locked decisions | Aligned as design commitments | C++20/x86, Lua-only mod API, WPF manager, ZIP packages, fingerprinted build maps, GPL license and provenance ledger exist. Checked decisions about custom towers, backups, and offline enforcement describe v1 scope, not delivered functionality. |
| 1: Foundation | Substantially aligned | CMake rejects non-Windows/non-x86 targets; Debug/Release/analysis presets, pinned dependency revisions/hashes, notices, manager solution, schemas, samples, ignore rules, and build/test scripts exist. Current builds pass. Clean-checkout reproducibility was not re-proven. |
| 2: Bootstrap/lifecycle | Mostly aligned; shutdown integration gap | The minimal proxy forwards the current WinINet import to the system DLL and loads a separate runtime. Logging precedes compatibility and hooks. The fixture compares real/proxied results, verifies unchanged executable bytes, and exercises missing-runtime failure. Normal process exit is not connected to orderly loader shutdown; see finding 3. |
| 3: Compatibility/hooks | Aligned within tested scope | Executable and asset hashing precede hook setup; versioned maps, named resolution, section/pattern validation, prerequisites, diagnostics, and rollback exist. Current inspector and fixtures pass; recorded no-mod launch acceptance exists. |
| 4: Lua/packages | Completion overstated | Isolated states, lifecycle API, restricted libraries, config/storage/localization/resources, archive validation, manifests, and dependency ordering exist. The checked claims for every host boundary and callback budgets fail the probes below. Live shutdown is also unconnected. |
| 5: Manager workflow | Mostly implemented; vanilla launch gap | Discovery, identity/hash validation, ownership records, repair/uninstall, local package workflows, profiles, ordering, configuration, launch history, diagnostics, and LocalAppData storage are present. Recorded acceptance and current integration tests support these. Vanilla launch does not guarantee absence of mods; see finding 4. |
| 5R: Reliability revamp | Mostly aligned; log bound overstated | Settings, migration, profile lifecycle, configuration editor, bundle discovery, loader health/version checks, operation journals, preflight, rollback, readiness routing, and optional live logs exist with tests and a recorded fresh-state WPF walkthrough. Tail reads bound line count, not bytes; see finding 5. |
| 6: Gameplay | Correctly unfinished, with overbroad checked items | Match/round/cash/lives/tower/bloon events, tower/bloon handles, selected cancellations, lives replacement, and three property setters exist. Recorded acceptance supports the listed loss/tower/property scenarios. Other live object kinds and complete-match coverage remain missing. |
| 7: Custom towers | Correctly unfinished | No `btd5.towers.register`, custom tower definition schema, shop insertion, upgrade graph, or complete custom tower sample. Factory symbols and capability names are groundwork only. |
| 8: Assets/localization | Correctly unfinished at pipeline level | Phase 4 can package/read assets and load an English localization table. Native rendering/audio decoding, resource caching/ownership, content references, asset limits, and fallback/formatting pipeline are absent. Existing resource reads do not complete this phase. |
| 9: Offline/saves/recovery | Correctly unfinished, with partial overlap | Launch acknowledgement and a developer firewall script exist. Runtime network guards, save backup/restore/retention, and crash-triggered mod disabling do not. Launch history is not a crash recovery journal. Vanilla launch is already present in Phase 5 but needs finding 4 fixed; a dedicated disable-all recovery workflow was not found. |
| 10: Hardening/release | Unfinished, but several tasks already have groundwork | Release staging, ZIP/checksum generation, API/package/recovery documentation, and multiple event examples exist. Fuzzing, long-session/performance gates, complete compatibility matrix, custom tower sample, and final release-content checks are not established. Release preparation is not proof of the v1 gate. |

## Findings, in priority order

### 1. P1: Lifecycle callback lookup executes unprotected Lua

`LuaMod::invoke` calls `lua_getglobal` before entering `execute_at_stack_top`
(`src/native/runtime/lua_mod.cpp:108`). Mods retain `setmetatable` and `_G`.
If a callback is absent and `_G.__index` exists, lookup invokes that metamethod
before the host installs its budget or calls `lua_pcall`.

The isolated probe installed an `__index` that logs, executes a 100,000-iteration
loop, and returns an empty callback. With a 2,000-instruction limit, invocation
still succeeded and the metamethod ran. An error or nonterminating loop in the
same location can escape the intended containment boundary. The recent raw
event-payload read fix does not cover lifecycle lookup.

Reopen Phase 4's every-boundary containment claim. Use raw callback lookup or
perform lookup inside the protected call, and audit allocation-capable host
operations before claiming that every Lua boundary is contained.

### 2. P1: Nested callbacks remove the outer instruction hook

`execute_at_stack_top` overwrites the shared budget/deadline, then unconditionally
clears the Lua hook when its call finishes
(`src/native/runtime/lua_mod.cpp:818`). It does not restore the enclosing
callback's budget, deadline, or hook.

The isolated probe used the host logging callback to re-enter `invoke` once,
then executed a 100,000-iteration loop in the outer callback. With a budget of
2,000, the nested case succeeded while an otherwise equivalent control callback
failed its budget. This directly exercises the reentrant host contract already
used by the recursive-event tests; it is not a claim that the current production
logger re-enters Lua.

Reopen Phase 4's budget guarantee and narrow Phase 6's recursive-dispatch test
claim. Preserve/restore enclosing execution state, with regression coverage for
both successful and failing nested callbacks.

### 3. P1: Normal game exit never requests orderly Lua shutdown

`BTD5Loader_Shutdown` invokes `on_shutdown`, invalidates handles, removes hooks,
and clears session extraction files (`src/native/runtime/runtime.cpp:1127`).
Repository-wide reference inspection found its export/definition but no caller
from the bootstrap, manager, or normal game exit path. Both DLL entry points
handle process attach only. The Lua host destructor closes the state without
invoking `on_shutdown`. Startup rollback has a separate invocation path.

The lifecycle tests explicitly call `mod.invoke("on_shutdown")`, proving the
callback API in isolation rather than its normal game integration. Mods relying
on orderly cleanup will not receive that lifecycle callback through ordinary
game exit as currently wired.

Keep the API-exists item, but reopen the clean live-shutdown acceptance claim.
Connect an orderly native exit boundary and record a real exit test covering
exactly-once callback delivery and session cleanup.

### 4. P1: Vanilla launch can consume a recent modded handoff

`GameLaunchService.LaunchVanillaAsync` only opens `steam://run/306020`
(`src/manager/BTD5ModLoader.Manager.Core/GameLaunchService.cs:215`). It does not
disable the installed proxy or establish a vanilla launch marker.

When the runtime receives no `BTD5ML_ACTIVE_PROFILE` variable, it accepts the
shared `runtime/active-profile.json` if its timestamp is within 60 seconds
(`src/native/runtime/runtime.cpp:322`). If the loader is installed in the
registered Steam copy, a quick vanilla retry after a modded launch can therefore
load the previous enabled mods. Testing only a disposable copy does not cover
this ordinary installation scenario.

The manager test checks the Steam URI request, not the resulting absence of Lua
mods. Reopen the Phase 5 vanilla guarantee and add an explicit, reliable vanilla
launch path that cannot consume a stale or recent modded handoff. The later
Phase 9 recovery gate must include this sequence.

### 5. P2: Log tail reads have no byte or line-length bound

`ReadRuntimeLogAsync` scans backwards until it finds enough line breaks, then
uses `ReadLineAsync` to allocate complete strings
(`src/manager/BTD5ModLoader.Manager.Core/GameLaunchService.cs:273`). A very large
file with one or a few oversized lines requires reading the entire file and
materializing the oversized strings, even when one line is requested.

This does not satisfy Phase 5R's bounded-tail guarantee for large logs. Add a
maximum byte window and a maximum returned line size, with a truncation marker
and coverage for huge lines and incomplete trailing records.

### 6. P2: Phase 6's checked wrapper/test wording exceeds live coverage

The registry and Lua userdata support seven object-kind tags, but the runtime
only creates live handles for towers and bloons
(`src/native/runtime/runtime.cpp:147`, `:186`). Match/round notifications carry
no live match/round/player wrappers, and no attack/projectile lifecycle bridge
is implemented. Split the checked generic-wrapper foundation from unfinished
live integration for the remaining five object kinds.

Likewise, accepting every event name in mock tests does not verify every native
event boundary. The native test target does not compile `runtime.cpp`, so its
cross-mod mutation propagation and lifecycle wiring are not directly covered
by those fixtures. The two new counterexamples above demonstrate why the
checked universal coverage statement is too broad. Describe the tested subset
and retain explicit integration work.

## Tracker and documentation corrections

- Reopen the Phase 4 budget, boundary-containment, and orderly-shutdown claims;
  record these as repairs to an earlier gate before advancing feature work.
- Reopen the Phase 5 vanilla-launch guarantee and the Phase 5R bounded-log item.
- Use `[~]` for the Phase 6 cancellation/mutation and setter parent items, since
  their checked children show implementation in progress. Keep its overall gate
  open. Bloon leak cancellation still lacks recorded interactive cancellation
  acceptance; native life gains remain fixture-only in the lives record.
- Split generic object wrappers from live object integration and replace
  universal automated coverage wording with the actual tested scope.
- Mark partial overlap in later task descriptions without passing their gates:
  event examples and release ZIP/checksum tooling already exist. The strict
  no-next-phase-work rule needs to distinguish shared scaffolding from complete
  feature implementation to describe this repository accurately.
- Update README's capability table: it still says property setters and mutable
  fields are not implemented and lists only lives cancellation. The code and
  Phase 6 records already include lives replacement, tower upgrade/sale and
  bloon leak cancellation, tower pop count/sell price, and bloon health setters.
- Release packaging copies a summary notices file, but the stage/release scripts
  do not copy each dependency's full license text as THIRD_PARTY_NOTICES requires.
  Keep that release-content task open; ZIP/checksum generation alone is not its
  acceptance gate.

Recommended next work is the earlier-phase correctness repairs above, followed
by the remaining Phase 6 live integrations and complete-match acceptance.
