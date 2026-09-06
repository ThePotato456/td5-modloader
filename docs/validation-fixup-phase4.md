# FIXUP Phase 4 — Orderly normal-exit shutdown

Date: 2026-09-05. Tested revision: `59bda5a` plus the uncommitted Phase 2–4
changes delivered with this record. The copied-game acceptance used the Debug
runtime with SHA-256
`850B0B212B008D0872FA130CD6D0956223CDE2EE252714E2FCF50A35915297CC`.

## Boundary and lifetime review

Independent read-only inspection of the fingerprinted executable identified
the CRT call immediately after `game.main` returns. `game.exit.call` resolves
to RVA `0x527447`; its original no-argument predicate resolves to `0x527EBA`.
The preceding main call is at `0x527440`. The CRT saves main's result, calls
the predicate, and selects its exit continuation from that boolean result.
The predicate checks the executable's managed-image directory. No source from
the unlicensed decompilation repository was copied or translated.

The resolver verifies both required patterns and the hook verifies the relative
CALL target before replacement. Its four-byte displacement is aligned and
replaced atomically; the opcode remains unchanged. Instruction-cache flushing
and page-protection restoration follow each patch. Tests execute both original
predicate outcomes, self-removal, repeated removal, rejection of a mismatched
target, and a throwing cleanup callback. The original predicate result is
preserved even after removal. The exit hook is installed first and removed last.

Normal shutdown executes on the thread returning from game main, before CRT
teardown and outside DllMain/loader lock. It does not assume game objects remain
alive: wrapper generations and native property access are invalidated before
Lua shutdown. The loader waits for its initialization worker and drains admitted
Lua work. Every production frame/event/timer path uses `ModDispatchGate`.
The loading-state transition also holds this gate, preventing a first frame
from delivering readiness before the mod collection is populated.

An explicit shutdown request stops and drains Lua work but leaves native hooks
and trampolines installed until this normal-exit boundary. This avoids freeing
a trampoline while a native detour is still returning outside the Lua gate.
Repeated requests serialize with cleanup and do not redeliver callbacks.
Unloading the runtime DLL while the game is active is unsupported.

Loaded mods receive `on_shutdown` in reverse load order. Startup rollback uses
the same production helper; empty/partially loaded collections are supported.
A failed callback does not prevent later callbacks, and clearing the collection
prevents duplicate delivery. Logging, storage, and packaged resources remain
available during callbacks. Only the current session extraction directory is
removed after state destruction. Filesystem cleanup errors are logged instead
of reporting completion. Forced termination/crashes cannot guarantee callbacks
or cleanup; recovery remains Phase 9 work.

## Automated checks

- Debug and Release: all three CTest entries passed, including isolated Lua
  boundary probes and the real proxy/runtime fixture; managed integration tests
  passed.
- Production helper tests verify reverse callback order, containment of a Lua
  shutdown error, repeated cleanup, and an empty partial initialization.
- A concurrent production-gate test holds active dispatch while another thread
  requests stop: cleanup cannot acquire the gate until dispatch leaves, and
  subsequent dispatch is refused. Repeated stop requests are idempotent.
- The proxy fixture calls the actual runtime shutdown export twice after its
  unsupported-image startup path and requires one request/completion log pair
  and `ShuttingDown` state.
- All 33 supported-build symbols resolve, including the two exit symbols.
- Native/managed static analysis, managed formatting, and whitespace checks pass.

## Copied-game acceptance

The smoke harness now accepts `--expect-shutdown`. After sample load, readiness,
and a deterministic timer, it requests normal window close and waits for process
exit. Passing requires exit code zero, exactly one sample shutdown message,
exactly one runtime shutdown request/completion pair, and removal of the new
session without removing pre-existing sessions. Forced cleanup on a failed run
cannot satisfy this mode.

Two successive runs of the final runtime passed at approximately 20:55:57 UTC
and 20:56:18 UTC. Persistent sample launch counts advanced from 6 to 7. Both
used normal window close through the production exit hook; neither passing run
required forced termination. The tests used an isolated copy and manager state,
the lifecycle package, and development Steam App ID environment variables.
This is normal window-exit acceptance, not a complete-match or Steam vanilla
launch acceptance.

Earlier failed attempts are not acceptance evidence: one restarted through
Steam into the already-installed runtime and required test-process cleanup;
another exposed the readiness/loading race fixed above. A preliminary successful
normal exit also exposed harness handle/session-accounting errors, now fixed.
The final two passes were performed after those fixes. Raw game output and
session logs remain ignored local artifacts because they can contain private
Steam information.

Before/after hashes match:

| File | SHA-256 |
| --- | --- |
| `BTD5-Win.exe` | `BDC4F4AEC679F51B8763FF7FE517A2556E392D99576045ECE117FCAFDDA27B70` |
| `Assets/BTD5.jet` | `906AA89D690C27664CE47A1A2E3EAC756D7CF551FE3E1669EC22AE814346B9A8` |

Phase 4 is complete. FIXUP Phase 5 vanilla isolation is next; gameplay feature
work and the remaining FIXUP gates stay open.
