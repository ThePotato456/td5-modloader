# Phase 6 live tower property validation

Date: 2026-08-30

This validation covers the first game-specific getters and setters on
lifetime-checked Lua object wrappers: tower pop count, tower sell price, and
bloon health.

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

The sale payout routine at RVA `0x424630` reads the signed 32-bit base sell
price from tower offset `0x144` before applying the active refund multiplier.
The bloon damage commit at RVA `0x23C7EC` reads, subtracts from, writes, and
checks the 32-bit floating-point health field at bloon offset `0x224`. Complete
patterns for both consumers are required startup symbols. Direct field access
is enabled only after those patterns resolve uniquely on the fingerprinted
build.

## Lua contract and automated fixture

Tower and bloon wrappers expose:

```lua
tower:pop_count(): integer
tower:set_pop_count(value: integer): boolean
tower:sell_price(): integer
tower:set_sell_price(value: integer): boolean
bloon:health(): number
bloon:set_health(value: number): boolean
```

The setter accepts only Lua integers in `0..2147483647`. Both methods resolve
the wrapper's ID, generation, scene, and tower kind immediately before native
access. Stale wrappers, bloon wrappers, invalid types, negative values, values
above the signed 32-bit maximum, missing accessors, and host failures produce a
contained Lua error.

Native fixtures verify successful read/write/readback, strict type and range
rejection, wrong-kind rejection, stale-wrapper rejection, and preservation of
the original value after rejected calls.
Sell price uses the same signed integer range as pop count. Health accepts only
finite, nonnegative values representable by a 32-bit float. Health replacement
does not synthesize damage or pop events; the game consumes it through its
normal update and damage paths.

## Interactive copied-game acceptance

The staged Release smoke workflow launched the ignored copied game through
Steam with the lifecycle sample's opt-in `mutate_tower_pop_count` setting. In an
ordinary single-player match, a newly placed tower reported pop count `0`. Lua
called `set_pop_count(123)`, immediately read back `123`, and logged
`Lifecycle Sample mutated tower.pop_count 0->123` at
`2026-08-30T17:49:44.634Z`.

The dedicated `--expect-tower-pop-count` verifier reported `LIVE_SMOKE_PASS`,
closed only its exact BTD5 process, and left no game process running.

A second dedicated Release run enabled `mutate_direct_properties`. The harness
required a placed tower's sell price to read back as `777` and a spawned bloon's
health to read back one point above its original value. Both Lua records
appeared, `--expect-direct-properties` reported `LIVE_SMOKE_PASS`, and the
harness closed only its exact game process.

## Scope

This result proves immediate pop-count and sell-price access on a valid live
tower and health access on a valid spawned bloon in the supported build. It
does not claim persistence after an upgrade, sale, layer split, scene
transition, save/reload, or game restart. Setting health to zero does not
promise an immediate pop; native game logic decides when the replacement is
observed. The wrappers deliberately expose no native address. Additional
properties require their own verified native consumer or accessor, validation
contract, fixtures, and copied-game acceptance.
