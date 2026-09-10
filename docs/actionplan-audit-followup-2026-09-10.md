# ACTIONPLAN audit follow-up

Reviewed on 2026-09-10 against the FIXUP work through the current Phase 8
working tree. This follow-up closes the six findings from the
[2026-09-05 audit](actionplan-audit-2026-09-05.md) without expanding historical
manual evidence or declaring the unfinished ACTIONPLAN phases complete.

| Finding | Repair | Regression and acceptance evidence | Status |
| --- | --- | --- | --- |
| 1. Lifecycle lookup escaped Lua containment | Lifecycle lookup moved behind protected raw-global access; every setup/load/invoke/event/timer boundary uses the protected trampoline. | [FIXUP Phase 2](validation-fixup-phase2.md), isolated Lua boundary suite | Closed |
| 2. Nested callbacks reset outer limits | Nested dispatch shares the outer instruction allowance, deadline, hook, and recursion state. | [FIXUP Phase 3](validation-fixup-phase3.md), nested callback regressions | Closed |
| 3. Normal exit skipped orderly shutdown | The verified main-return boundary requests shutdown; the dispatch gate drains work and invokes reverse-order cleanup exactly once. | [FIXUP Phase 4](validation-fixup-phase4.md), proxy fixture and two normal-exit copied-game runs | Closed |
| 4. Vanilla launch could consume stale mod intent | Modded launches use process/start-time-bound, claimed tickets; vanilla has no ticket and clears inherited data-root state. | [FIXUP Phase 5](validation-fixup-phase5.md), registered-Steam modded/vanilla/modded acceptance | Closed |
| 5. Log tail was bounded only by line count | Each refresh captures one file snapshot, reads at most 1 MiB, and keeps at most 4 KiB per returned record with visible annotations. | [FIXUP Phase 6](validation-fixup-phase6.md), virtual 8 GiB stream tests and WPF acceptance | Closed |
| 6. Gameplay wording exceeded evidence | Production profile coordination is tested and a capability matrix separates fixture, historical live, and unfinished coverage. | [FIXUP Phase 7](validation-fixup-phase7.md), [complete-match record](validation-phase6-complete-match.md), [leak cancellation](validation-phase6-bloon-actions.md) | Closed |

## Release-content repair

Phase 8 adds exact pinned license texts and a final-ZIP checker. A synthetic
release candidate passed required-content, checksum, path, text, and nested
sample checks. Negative candidates missing Lua's license or containing
`BTD5-Win.exe` were rejected. Debug and Release suites, native/managed analysis,
formatting, and whitespace checks passed in the working tree.

A detached checkout of committed revision `7cf2b91` was configured from empty
build directories with the documented MSVC toolchain and repository-provided
.NET 10.0.400 SDK. Debug and Release builds and all tests passed. The first
attempt exposed that the ignored local SDK is not present inside a checkout;
adding that documented SDK directory to `PATH` completed the run. Because the
Phase 8 changes are not committed yet, a final clean-checkout run of the repaired
revision remains required after commit before FIXUP itself can close.

## Remaining ACTIONPLAN scope

ACTIONPLAN Phase 6 remains partial: native life-gain acceptance, full-match
mutation coverage, future match/round/player/attack/projectile wrappers, and
remaining destruction paths are open. Custom towers, asset pipelines, in-process
offline/save/recovery safeguards, fuzzing, performance work, compatibility
coverage, end-user release acceptance, and the v1 gate also remain open. No tag,
push, publication, or v1 release is authorized or implied by this audit closure.
