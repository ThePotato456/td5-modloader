# FIXUP Phase 2 — Lua boundary containment

Date: 2026-09-05. Baseline: `59bda5a`, plus the Phase 2 implementation and tests
delivered with this record. This is native fixture and source-review acceptance;
no copied-game shutdown or complete-match acceptance is claimed.

## Implementation review

Lua remains the pinned 5.4.9 source, compiled as C++ so its built-in exception
handling unwinds host RAII objects. The reviewed source is `ldo.c`'s
`__cplusplus` exception branch. The public mod interface remains Lua only; the
private host includes Lua headers with matching C++ linkage. This build choice
is required, not interchangeable with a C/long-jump Lua library.

Each allocation-capable host entry uses a zero-upvalue C trampoline under
`lua_pcall`. Entry reserves stack space using the non-throwing `lua_checkstack`
path; pushing the trampoline and its light-userdata context does not allocate.
The host restores its entry stack top after success or error. Error extraction
uses existing string errors without coercing arbitrary error objects. Diagnostic
sink exceptions derived from `std::exception` are contained.

| Boundary | Protection and failure behavior |
| --- | --- |
| State allocation | `lua_newstate` returns null on allocation failure; no script can run. |
| Sandbox/API setup | Protected trampoline encloses library opening, closures, tables, and registration; failure closes the partial state and leaves `valid()` false. |
| Script load | Protected entry encloses parsing and execution; text-only compilation is retained. |
| Lifecycle lookup | Protected raw global read; missing callbacks do not invoke global metatables. Invalid or failing callbacks retain disabling behavior. |
| Event construction | Protected entry encloses tables, strings, references, and userdata; failed preparation rejects dispatch and restores event depth. |
| Event callbacks/readback | Existing protected callback calls and raw payload reads remain. A scoped registry reference releases the event even if readback fails. |
| Timer dispatch | Protected entry encloses dispatch; individual callbacks retain their own error handling. Registration owns its reference until transfer succeeds. |
| Host API/object methods | Guarded C functions convert standard C++ host exceptions to Lua errors inside callback protection. Lua errors unwind local strings, streams, and other RAII objects. |
| Diagnostics and accessors | Error reporting and status queries do not allocate on the Lua heap or invoke Lua code. |
| State destruction | No mod-created coroutines or registered user finalizers can survive for unprotected destruction. Lua-owned built-in cleanup remains. Normal-game-exit callback wiring is still FIXUP Phase 4. |

The sandbox no longer opens the coroutine library. Its replacement
`setmetatable` rejects raw `__gc` and `__close` fields and preserves protected
metatables. Adding `__gc` after initial attachment does not enroll a table for
finalization; subsequent attachment is rejected. A mod can still use ordinary
metatables. Mutating an attached metatable to introduce `__close` can only execute
through an active Lua call: no suspended coroutine can carry that execution to
state destruction. No guarantee is made for arbitrary third-party native code,
which the mod API does not expose.

Nested callback budget accounting is deliberately not marked repaired here;
FIXUP Phase 3 remains open. This phase establishes protected error/allocation
boundaries and removes unsupported execution paths.

## Regression acceptance

`btd5loader_lua_boundaries` runs the hidden boundary cases in a separate CTest
process with a 30-second timeout so a reverted lookup loop cannot hang the suite.
The cases verify:

- Lua errors execute C++ stack-object destructors.
- Infinite and throwing `_G.__index` functions never run during missing-callback
  lookup; a failing real callback is disabled while another remains usable.
- Small heap limits fail setup safely. Repeated oversized event payloads fail
  without disabling the handler or preventing subsequent valid dispatch.
- Oversized script compilation fails without breaking a subsequent callback;
  an independent healthy Lua state remains functional.
- Throwing host logging and non-string Lua errors remain contained across
  lifecycle, event, and timer calls, including a failing diagnostic sink.
- Coroutines are absent, finalizer attachment/re-attachment is refused, protected
  metatables remain protected, and destruction completes with a late `__gc` field.

The existing suite also covers resource reads, storage, object setters, strict
payload types, stale wrappers, event metamethod readback, and runaway callbacks.

Debug and Release test scripts passed, including the native suite, isolated
boundary process, proxy fixture, and manager integration tests. The final
userdata-allocation regression was rebuilt and rerun in both configurations.
Native/managed static analysis, formatting, and whitespace checks passed.
Existing CMake dependency deprecation notices do not indicate failures in the
changed code. The source-review matrix above records document acceptance of the
Phase 2 gate; it does not close Phase 3's nested-budget guarantee.
