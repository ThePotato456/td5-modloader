# Phase 6 live tower property validation

Date: 2026-08-30

This validation covers the first game-specific getter and setter on a
lifetime-checked Lua object wrapper: tower pop count.

## Binary research and symbol validation

Read-only disassembly of the fingerprinted Steam Win32 4.8 executable found a
unique tower pop-count getter at RVA `0x28E020`. It returns the signed 32-bit
value at tower offset `0xEC`. A unique setter at RVA `0x28A690` writes its
signed 32-bit argument to the same offset. The two functions occur as adjacent
entries in the live tower vtable, after the tower cleanup entry.

The supported-build symbol map now resolves both complete instruction patterns
as required symbols. Runtime startup fails closed before Lua mods load if either
pattern is missing, ambiguous, or fails validation. The Release symbol
inspector resolved both functions uniquely against the ignored copied game.

## Lua contract and automated fixture

Tower wrappers expose:

```lua
tower:pop_count(): integer
tower:set_pop_count(value: integer): boolean
```

The setter accepts only Lua integers in `0..2147483647`. Both methods resolve
the wrapper's ID, generation, scene, and tower kind immediately before native
access. Stale wrappers, bloon wrappers, invalid types, negative values, values
above the signed 32-bit maximum, missing accessors, and host failures produce a
contained Lua error.

Native fixtures verify successful read/write/readback, strict type and range
rejection, wrong-kind rejection, stale-wrapper rejection, and preservation of
the original value after rejected calls.

## Interactive copied-game acceptance

The staged Release smoke workflow launched the ignored copied game through
Steam with the lifecycle sample's opt-in `mutate_tower_pop_count` setting. In an
ordinary single-player match, a newly placed tower reported pop count `0`. Lua
called `set_pop_count(123)`, immediately read back `123`, and logged
`Lifecycle Sample mutated tower.pop_count 0->123` at
`2026-08-30T17:49:44.634Z`.

The dedicated `--expect-tower-pop-count` verifier reported `LIVE_SMOKE_PASS`,
closed only its exact BTD5 process, and left no game process running.

## Scope

This result proves immediate pop-count access on a valid live tower in the
supported build. It does not claim persistence after an upgrade, sale, scene
transition, save/reload, or game restart. The wrapper deliberately exposes no
native address. Additional properties require their own verified native
accessor, validation contract, fixtures, and copied-game acceptance.
