# FIXUP Phase 3 — Nested callback limits

Date: 2026-09-05. Reviewed revision: `59bda5a` plus the Phase 2 and Phase 3
changes delivered with this record. Acceptance is source review and native
fixtures; no new live-game acceptance is claimed.

## Execution policy and review

`LuaMod::execute_at_stack_top` starts a budget only at callback depth zero.
Nested lifecycle, event, and timer callbacks share the current mod's remaining
instruction allowance and absolute deadline. Their instructions and elapsed
time are therefore charged to the enclosing callback. Neither nesting nor an
inner error grants additional instructions or time.

The outermost call saves the pre-existing Lua hook/mask/count configuration,
installs the budget hook, and restores the saved configuration on exit through
an RAII scope. Nested calls leave the hook untouched, preserving its partially
used instruction countdown. The same scope restores callback depth after Lua
success or failure. Refusing entry at the recursion limit changes no active
execution state.

Instruction/time exhaustion is retained until the outermost callback completes.
Consequently, a host adapter cannot swallow inner exhaustion and report the
enclosing callback as successful. Checks before and after calls also detect
expired deadlines for short callbacks that never reach a hook interval. Native
API work is cooperative: it cannot be interrupted mid-call, but its elapsed time
counts against the current deadline and is checked on return.

Later independent callbacks start fresh limits; separate mods keep independent
state. No coroutine resumption path is exposed after Phase 2. Lua stack-depth
checks and host callback/event recursion limits remain in place.

## Regression coverage

The isolated `btd5loader_lua_boundaries` CTest process has a 30-second timeout
and now includes these production-LuaMod scenarios:

- The audit's nested-success/over-budget outer loop and equivalent non-nested
  control both fail. A subsequent healthy callback succeeds.
- Repeated short nested calls cannot keep resetting a partial hook countdown.
- Nested lifecycle, event, and timer callbacks fail ordinarily or exhaust their
  budget; outer work still encounters the active instruction limit. Caught inner
  exhaustion also rejects a short enclosing callback.
- A slow nested host call expires the original deadline, and a nested call
  attempted after the parent deadline has already expired is refused.
- Recursion refusal leaves depth usable, and repeated independent calls recover
  after exhaustion. Another mod's healthy callback remains unaffected.

The reentrant logging adapter is deliberate test machinery exercising the host
contract, not a claim that the production file logger itself invokes Lua.

## Gate verification

Debug and Release suites passed, including native fixtures, the isolated
boundary process, the proxy fixture, and manager integration tests. The final
expired-parent-deadline case was also rebuilt and verified in Debug. Native and
managed static analysis passed. Formatting and whitespace checks accompany the
completed FIXUP gate. This repair does not close normal game exit, vanilla
isolation, or the complete-match gate.
