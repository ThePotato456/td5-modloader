# BTD5 Mod Loader — Correctness and Alignment Fixup Plan

Based on the [2026-09-05 ACTIONPLAN audit](docs/actionplan-audit-2026-09-05.md)
of commit `1d0bb85`.

This plan repairs guarantees that were marked complete too early and brings
tracking, tests, and documentation into agreement with the implementation.
It supplements [ACTIONPLAN.md](ACTIONPLAN.md); it does not replace the remaining
gameplay, custom tower, asset, offline protection, or v1 release work.

## Current status — 2026-09-05

| Phase | Status | Evidence / next work |
| --- | --- | --- |
| 1 — Baseline correction | Complete; committed as `59bda5a` | [Document review](docs/validation-fixup-phase1.md) |
| 2 — Lua boundaries | Complete | [Boundary review and regression results](docs/validation-fixup-phase2.md) |
| 3 — Nested limits | Complete | [Budget policy and regression results](docs/validation-fixup-phase3.md) |
| 4 — Orderly shutdown | Complete | [Boundary review, regression results, and two normal-exit runs](docs/validation-fixup-phase4.md) |
| 5 — Vanilla isolation | Complete | [Process-bound protocol, regressions, and registered-Steam acceptance](docs/validation-fixup-phase5.md) |
| 6 — Bounded logs | Not started | Bound scanned bytes and individual line size. |
| 7 — Gameplay coverage | Not started | Test production coordination and align capability documentation. |
| 8 — Release contents and closure | Not started | Package required notices, verify artifacts, and close the audit. |

Phases 2–3 passed Debug and Release suites, isolated Lua boundary regressions,
native/managed static analysis, formatting, and whitespace checks. Their source
reviews are recorded above; no new live-game acceptance is claimed for those phases.

Phases 4–5 additionally passed normal-exit and registered-Steam launch acceptance.
Vanilla now excludes Lua and game hooks, including after a recent modded launch
or an interrupted launch ticket. General crash recovery remains unfinished.

The sandbox now omits coroutines and rejects finalizer-bearing metatables at
attachment. Lua errors unwind C++ host objects, lifecycle lookup uses raw globals,
and nested callbacks share the enclosing instruction allowance and deadline.
Time limits are cooperative and cannot interrupt a native API call mid-execution.

Next: begin **Fixup Phase 6**. Log bounds,
remaining gameplay integration, and release closure are still open. New ACTIONPLAN
Phase 6 features remain paused until this fixup plan is complete.

## Tracker rules

- `[ ]` means not started, `[~]` means in progress, and `[x]` means complete.
- Complete fixup phases in order. Do not begin the next phase until the current
  phase's implementation gate is checked.
- Complete this fixup plan before resuming new Phase 6 features. Repairs and
  verification of existing behavior belong here; unfinished feature scope stays
  in ACTIONPLAN.
- A gate passes only when its listed automated checks pass and its manual
  acceptance or document/artifact inspection is recorded with the tested revision.
- Keep historical acceptance records. Add dated corrections and new evidence;
  do not rewrite past runs as proof of behavior they did not exercise.
- Passing existing tests alone does not close a reproduced gap. Add a regression
  that exercises the failing behavior and the actual production integration.
- Keep game binaries, assets, saves, packages, logs, backups, account identifiers,
  and machine-specific paths out of version control and release artifacts.
- Use isolated fixtures and copied-game acceptance where possible. Record which
  checks used mocks, the real runtime, the manager UI, or the game.

---

## Fixup Phase 1 — Correct the baseline and reopen unsupported claims

Original scope: ACTIONPLAN Phases 2, 4, 5, 5R, and 6; audit findings 1–6.

Completed 2026-09-05. [Document review and finding map](docs/validation-fixup-phase1.md).

- [x] Add a dated audit note to ACTIONPLAN explaining why earlier completed
  guarantees require repair before forward feature work resumes.
- [x] Reopen the Phase 4 instruction-budget and every-host-boundary containment
  claims, preserving checked items for APIs that actually exist.
- [x] Separate the implemented `on_shutdown` API from the missing normal-game-exit
  integration; reopen the live clean-shutdown acceptance claim.
- [x] Split Phase 5's launch item into modded launch, verified vanilla launch,
  and the other existing status/diagnostic operations; reopen vanilla assurance.
- [x] Reopen Phase 5R's bounded-tail guarantee while preserving its implemented
  visibility, refresh, and confirmed-clear behavior.
- [x] Mark partially implemented Phase 6 mutation and setter parent items `[~]`.
- [x] Split the seven-kind generic wrapper foundation from unfinished live
  integration for matches, rounds, players, attacks, and projectiles.
- [x] Replace the universal Phase 6 automated-coverage statement with explicit
  tested behaviors and outstanding production-integration checks.
- [x] Clarify that locked v1 decisions are scope commitments and that shared
  groundwork in later phases does not pass those phases' feature gates.

### Fixup Phase 1 implementation gate

- [x] Every audit finding maps to an open repair or explicitly unfinished feature.
- [x] A document review confirms no historical acceptance run is presented as a
  fresh test or evidence for a broader guarantee.
- [x] ACTIONPLAN formatting and checkbox conventions pass repository checks.

---

## Fixup Phase 2 — Contain Lua execution at every host boundary

Original scope: ACTIONPLAN Phase 4; audit finding 1.

Completed 2026-09-05. [Boundary review and verification](docs/validation-fixup-phase2.md).

Primary implementation: `src/native/runtime/lua_mod.cpp` and native Lua tests.

- [x] Make lifecycle callback lookup unable to execute mod-controlled metatable
  code outside protected execution. Define whether callbacks require raw global
  entries or whether lookup itself runs inside the protected call.
- [x] Audit state setup, API registration, script loading, lifecycle lookup,
  event construction/readback, timer dispatch, userdata creation, error handling,
  and state destruction for Lua execution or allocation outside protection.
- [x] Handle allocation failure at host boundaries without a Lua panic escaping
  into the game; keep error reporting usable after memory exhaustion.
- [x] Preserve the existing raw event-payload inspection and strict numeric
  validation when restructuring protected execution.
- [x] Define containment behavior for finalizers and coroutine execution exposed
  by the sandbox; record and close any gaps found during this review.
- [x] Add regressions for absent callbacks with `_G.__index`, throwing lookup
  metamethods, bounded runaway probes, and low-memory setup/event paths.
- [x] Verify that a failure reports mod ID and boundary, leaves the Lua stack
  usable where recovery is supported, and does not corrupt a healthy mod.

### Fixup Phase 2 implementation gate

- [x] The audit's lifecycle metatable probe cannot run outside protection.
- [x] Boundary/error/memory regressions pass in Debug and Release; use isolated
  subprocesses with timeouts for probes that could otherwise abort or hang tests.
- [x] A recorded boundary review identifies the protection mechanism for every
  host entry/exit path and documents any unsupported behavior explicitly.
- [x] Native static analysis passes for the changed code.

---

## Fixup Phase 3 — Preserve limits across nested callbacks

Original scope: ACTIONPLAN Phases 4 and 6; audit finding 2.

Completed 2026-09-05. [Budget policy and verification](docs/validation-fixup-phase3.md).

Primary implementation: `LuaMod::execute_at_stack_top` and recursive-event tests.

- [x] Give nested execution explicit state for instruction allowance, deadline,
  recursion depth, and the installed Lua hook.
- [x] Restore enclosing execution state after nested success, Lua error,
  instruction exhaustion, time exhaustion, and recursion refusal.
- [x] Define budget accounting so nested calls cannot replenish the enclosing
  allowance or extend its deadline. Document how nested work is charged.
- [x] Preserve the outer hook after inner execution returns; clear hooks only
  when the corresponding protected execution has finished.
- [x] Turn the audit's reentrant-host probe into a permanent regression with an
  equivalent non-nested control and a healthy second mod.
- [x] Cover nested lifecycle, event, and timer paths where supported, including
  caught inner errors followed by substantial outer work.

### Fixup Phase 3 implementation gate

- [x] Both nested and control probes stop at the documented budget rather than
  allowing the nested case to finish an over-budget loop.
- [x] Tests verify depth, deadline, allowance, and hook restoration after each
  failure path and after repeated dispatch.
- [x] Debug/Release Lua tests and static analysis pass.
- [x] A recorded review confirms the documented budget policy matches the
  production host and its reentrant test adapters.

---

## Fixup Phase 4 — Wire orderly shutdown into normal game exit

Completed 2026-09-05. [Shutdown review and acceptance](docs/validation-fixup-phase4.md).

Original scope: ACTIONPLAN Phases 2 and 4; audit finding 3.
Primary implementation: runtime lifecycle, bootstrap/exit integration, live smoke.

- [x] Identify and independently verify an orderly exit boundary in the supported
  build, including thread, object-lifetime, and hook-removal constraints.
- [x] Connect that boundary to runtime shutdown before Lua state destruction;
  do not rely on the manager forcibly terminating the game or on a fixture
  directly invoking `on_shutdown`.
- [x] Make shutdown idempotent across explicit requests, startup rollback,
  repeated exit notifications, and partially initialized runtime states.
- [x] Stop new frame/event/timer work and coordinate in-flight dispatch before
  destroying states or removing resources they can still access.
- [x] Deliver `on_shutdown` once per eligible loaded mod in documented order;
  contain a failing callback so other mods still receive shutdown.
- [x] Define wrapper availability during shutdown, then invalidate handles,
  release hooks/states, and remove only the current session's extracted files.
- [x] Document that forced termination and crashes cannot promise orderly Lua
  callbacks; retain crash recovery as unfinished ACTIONPLAN Phase 9 work.

### Fixup Phase 4 implementation gate

- [x] Automated lifecycle tests cover repeated shutdown, partial initialization,
  callback failure, and prevention of dispatch after shutdown begins.
- [x] A copied-game normal-exit run records exactly-once shutdown callbacks and
  session cleanup through the production exit path.
- [x] A second launch works and game executable/asset hashes remain unchanged.
- [x] Debug/Release suites and static analysis pass; the new exit acceptance is
  recorded separately from prior callback-only fixture evidence.

---

## Fixup Phase 5 — Make vanilla launch reliably exclude mods

Completed 2026-09-05. [Protocol review and Steam acceptance](docs/validation-fixup-phase5.md).

Original scope: ACTIONPLAN Phase 5 and future Phase 9 recovery; audit finding 4.
Primary implementation: `GameLaunchService`, active-profile handoff, bootstrap/runtime.

- [x] Define an explicit vanilla launch contract covering a loader installed in
  the registered Steam copy, as well as disposable copies.
- [x] Replace timestamp-only launch intent with a reliable protocol so a recent
  shared handoff cannot authorize an unrelated vanilla launch.
- [x] Preserve supported modded launching when Steam starts or restarts the game
  without inheriting the manager's environment variables.
- [x] Bind and consume modded launch intent appropriately; handle stale, future-
  dated, malformed, missing, and previously consumed handoffs safely.
- [x] Define behavior for concurrent launch requests and an already-running game.
  Ensure one request cannot silently change another request's launch mode.
- [x] If loader files must be temporarily changed, use verified ownership,
  reversible operations, and interruption recovery; preserve foreign files.
- [x] Report failure when vanilla isolation cannot be established instead of
  reporting success solely because Windows accepted a Steam URI.
- [x] Add production-path tests that establish the absence of Lua loading, not
  just the shape of the process-launch request.

### Fixup Phase 5 implementation gate

- [x] A modded launch followed immediately by vanilla launch cannot reuse its
  profile, including within the former 60-second fallback window.
- [x] Automated tests cover the handoff cases, concurrent requests, interrupted
  transitions, and subsequent successful modded launch.
- [x] Recorded manual acceptance exercises the Steam launch path with a loader
  installed where that path starts the game; copy-only URI tests are insufficient.
- [x] Vanilla acceptance shows no Lua callbacks or gameplay mutations, preserves
  game hashes, and leaves an actionable recovery state after simulated failure.

---

## Fixup Phase 6 — Bound log reads by bytes as well as lines

Original scope: ACTIONPLAN Phase 5R; audit finding 5.
Primary implementation: `GameLaunchService.ReadRuntimeLogAsync` and log UI.

- [ ] Set documented limits for bytes scanned, returned line count, and individual
  returned line size. Keep allocation bounded independently of total file size.
- [ ] Read oversized or unterminated lines without allocating their entire content.
- [ ] Represent truncation clearly while retaining useful recent log content.
- [ ] Handle UTF-8 boundaries, partial trailing records, concurrent appends,
  truncation/clearing, file replacement, and cancellation.
- [ ] Preserve optional live refresh and prevent overlapping refreshes from
  accumulating work while the log is visible.
- [ ] Add realistic fixtures with a large newline-free record, oversized JSON
  lines, many small lines, and a file changing during a read.

### Fixup Phase 6 implementation gate

- [ ] Tests demonstrate the specified byte/line bounds rather than asserting
  only the number of returned strings.
- [ ] A recorded manager check shows responsive opening, refreshing, hiding, and
  clearing with oversized logs and visible truncation feedback.
- [ ] Managed tests, analyzers, and formatting pass.

---

## Fixup Phase 7 — Align gameplay coverage and public capability claims

Original scope: ACTIONPLAN Phase 6; audit finding 6 and documentation drift.

- [ ] Build a coverage matrix for each existing event, cancellation, mutable
  field, and setter: contract, native boundary, fixture, and live acceptance.
- [ ] Exercise production dispatch coordination in tests, either through a
  testable shared component or an integration harness using the real runtime.
- [ ] Cover cross-mod lives replacement, invalid replacement retaining the last
  accepted value, cancellation precedence, callback ordering, and shutdown wiring.
- [ ] Keep future live match/round/player/attack/projectile wrappers explicitly
  open in ACTIONPLAN; generic kind tags do not complete their integration.
- [ ] Keep complete-match acceptance open, including duplicate-hook detection,
  teardown, stale objects, interactive bloon leak cancellation, and native life
  gains where current records cover only fixtures.
- [ ] Update README and Lua documentation to list the implemented cancellation,
  lives replacement, tower pop-count/sell-price, and bloon-health capabilities.
- [ ] Distinguish supported live behavior, fixture-only evidence, and planned APIs
  consistently across ACTIONPLAN, README, Lua reference, and sample documentation.

### Fixup Phase 7 implementation gate

- [ ] Every currently claimed capability has matching code and identified test
  evidence; missing live evidence remains visibly unfinished.
- [ ] New production-coordination tests pass in Debug and Release.
- [ ] A document review confirms no generic wrapper or event-name test is counted
  as proof of an unimplemented live integration.
- [ ] ACTIONPLAN Phase 6 remains open until its separate complete feature gate passes.

---

## Fixup Phase 8 — Repair release contents and close the audit

Original scope: existing packaging groundwork and ACTIONPLAN Phase 10.

- [ ] Stage the full required copyright/license texts for every shipped dependency
  from its pinned source, alongside the existing third-party summary and GPL text.
- [ ] Fail release preparation when a required notice is missing; verify the
  contents of the final ZIP rather than only the staging directory.
- [ ] Add an artifact-content check excluding game files, private data, local paths,
  installed user mods, and unrelated stale staging output. Allow source-controlled
  example packages intentionally assembled by the release workflow.
- [ ] Record existing ZIP/checksum tooling and event samples as partial Phase 10
  groundwork without marking the complete v1 release gate passed.
- [ ] Re-run Debug/Release suites, static analysis, and formatting after the repairs.
- [ ] Record a clean-checkout build using the documented toolchain to refresh the
  foundation evidence without relying on this workspace's generated output.
- [ ] Add a dated audit follow-up linking each closed finding to its implementation,
  regression coverage, and required manual acceptance.
- [ ] Restore only the ACTIONPLAN checkboxes whose guarantees now have evidence;
  retain unresolved features and later-phase gates as open.

### Fixup Phase 8 implementation gate

- [ ] The generated archive contains required notices and passes artifact checks.
- [ ] All fixup gates have recorded evidence and the complete automated checks pass.
- [ ] ACTIONPLAN, FIXUP, README, and API documentation agree on delivered behavior.
- [ ] Remaining work resumes at ACTIONPLAN Phase 6; this audit closure does not
  authorize passing Phase 7–10 gates or tagging v1.
