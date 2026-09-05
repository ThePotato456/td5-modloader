# FIXUP Phase 1 — Baseline correction review

Date: 2026-09-05. Reviewed baseline: commit `9823435`, with the documentation
changes delivered for FIXUP Phase 1. No product code changed in this phase.

## Audit-to-tracker mapping

| Audit finding | Corrected ACTIONPLAN status | Open repair |
| --- | --- | --- |
| 1: Unprotected lifecycle lookup | Phase 4 containment is in progress; API availability remains checked. Broader boundary acceptance is open. | FIXUP Phase 2 |
| 2: Nested execution removes outer limits | Phase 4 limits and Phase 6 recursion guarantees are in progress; ordinary callback/depth fixtures remain credited. | FIXUP Phase 3 |
| 3: No normal-exit shutdown caller | Phases 2 and 4 now require production exit integration and live acceptance; explicit fixture callback invocation remains checked. | FIXUP Phase 4 |
| 4: Vanilla launch reuses modded intent | Phase 5 separates modded launch, the existing Steam button, and unfinished vanilla isolation, with an explicit gate. | FIXUP Phase 5 |
| 5: Tail reads lack byte bounds | Phase 5R preserves optional viewing/clearing and line-count support; byte/line-size bounds and responsiveness acceptance are open. | FIXUP Phase 6 |
| 6: Wrapper/coverage claims exceed integration | Phase 6 separates generic kinds from five unfinished live wrapper integrations, narrows existing test evidence, and opens production coordination coverage. | FIXUP Phase 7; remaining live features stay in ACTIONPLAN Phase 6 |

Additional audit follow-ups retain their existing owners: public capability
documentation in FIXUP Phase 7, and release notices/artifact checks and refreshed
clean-checkout evidence in FIXUP Phase 8. Later ACTIONPLAN feature gates remain
open. These are tracked obligations, not repairs performed by this phase.

## Document acceptance

The review compared the revised ACTIONPLAN against the dated alignment audit and
FIXUP Phase 1 checklist. All six findings now map to open repairs. Partially
implemented guarantees use `[~]`, existing API/fixture support retains `[x]`, and
missing integrations and acceptance scenarios use `[ ]`.

The old Phase 5R forward-work authorization is replaced by a historical
acceptance note and the current FIXUP prerequisite. Locked v1 decisions now
explicitly describe scope commitments. Shared groundwork does not imply a
completed later feature or an exception to the remaining feature gates.

Historical validation documents were left intact. ACTIONPLAN now distinguishes
direct shutdown invocation from normal exit, loss-path lives replacement from
fixture-only gains, and native leak cancellation fixtures from missing live
cancellation acceptance. It also keeps complete-match and remaining destruction
path checks open. No historical run is represented as newly executed here.

## Verification

- Repository formatting check: `scripts/check-format.ps1`.
- Whitespace check: `git diff --check`.
- Checklist review: all FIXUP Phase 1 tasks and gates are complete; Phases 2–8
  remain open. ACTIONPLAN's reopened gates are not marked repaired.

This is document acceptance only. No game, manager walkthrough, runtime test,
or implementation repair is claimed by this record. The next authorized scope
when continuing FIXUP is Phase 2, containment at Lua host boundaries.
