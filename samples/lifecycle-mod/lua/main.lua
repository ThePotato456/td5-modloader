local retained_match_tower
local retained_match_bloon
local retained_match
local function log(message)
    btd5.log("info", message)
end

-- The mutation match turns on every implemented mutation for a whole match:
-- each cancellation fires once and the next attempt is allowed through.
local mutation_match = btd5.config.get("test_mutation_match") == "true"
local function enabled(setting)
    return mutation_match or btd5.config.get(setting) == "true"
end

btd5.events.on("match.starting", function(event)
    assert(event.match:is_valid() and event.match:kind() == "match")
    retained_match = event.match
    log("Lifecycle Sample observed match.starting")
end)

btd5.events.on("match.started", function(event)
    assert(event.match:is_valid() and event.match:id() == retained_match:id())
    log("Lifecycle Sample observed match.started")
end)

btd5.events.on("match.ending", function(event)
    assert(event.match:is_valid() and event.match:id() == retained_match:id())
    log("Lifecycle Sample observed match.ending")
end)

btd5.events.on("match.ended", function(event)
    assert(not event.match:is_valid())
    assert(not retained_match:is_valid())
    log("Lifecycle Sample observed match.ended")
    if enabled("test_complete_match") then
        assert(retained_match_tower and not retained_match_tower:is_valid())
        assert(retained_match_bloon and not retained_match_bloon:is_valid())
        local ok = pcall(function() retained_match_tower:pop_count() end)
        assert(not ok)
        log("Complete match test retained objects stale after teardown")
    end
end)

local active_round
local previous_round

btd5.events.on("round.starting", function(event)
    if previous_round then assert(not previous_round:is_valid()) end
    assert(event.round:is_valid() and event.round:kind() == "round")
    active_round = event.round
    log("Lifecycle Sample observed round.starting")
end)

btd5.events.on("round.started", function(event)
    assert(event.round:is_valid() and event.round:id() == active_round:id())
    log("Lifecycle Sample observed round.started")
end)

btd5.events.on("round.ending", function(event)
    assert(event.round:is_valid() and event.round:id() == active_round:id())
    log("Lifecycle Sample observed round.ending")
end)

btd5.events.on("round.ended", function(event)
    assert(event.round:is_valid() and event.round:id() == active_round:id())
    previous_round = active_round
    active_round = nil
    log("Lifecycle Sample observed round.ended")
    local ended = event.round
    btd5.timer.after(1, function()
        assert(not ended:is_valid())
        log("Lifecycle Sample confirmed ended round became stale")
    end)
end)

local pending_cash
-- Cash changes many times per second in later rounds. Every update is still
-- checked, but only the first few are logged.
local CASH_LOG_LIMIT = 20
local cash_updates_logged = 0

btd5.events.on("cash.changing", function(event)
    assert(type(event.cash) == "number" and event.cash >= 0)
    pending_cash = event.cash
    if cash_updates_logged < CASH_LOG_LIMIT then
        log("Lifecycle Sample observed cash.changing cash=" .. string.format("%.0f", event.cash))
    end
end)

btd5.events.on("cash.changed", function(event)
    assert(type(event.cash) == "number")
    -- The native event carries the committed balance for both boundaries.
    assert(pending_cash == nil or event.cash == pending_cash)
    pending_cash = nil
    if cash_updates_logged < CASH_LOG_LIMIT then
        cash_updates_logged = cash_updates_logged + 1
        log("Lifecycle Sample observed cash.changed cash=" .. string.format("%.0f", event.cash))
    end
end)

local pending_lives_change
local pending_player_id
local native_gain_step = 0
local mutation_losses = 0

btd5.events.on("lives.changing", function(event)
    assert(event.player:is_valid() and event.player:kind() == "player")
    pending_player_id = event.player:id()
    assert(event.old_lives ~= event.new_lives)
    local transition = tostring(event.old_lives) .. "->" .. tostring(event.new_lives)
    log("Lifecycle Sample observed lives.changing " .. transition)
    if btd5.config.get("test_native_gains") == "true" and event.new_lives > event.old_lives then
        native_gain_step = native_gain_step + 1
        if native_gain_step == 1 then
            log("Native gain test normal " .. transition)
        elseif native_gain_step == 2 then
            event.cancelled = true
            pending_lives_change = nil
            log("Native gain test cancelled " .. transition)
            return
        elseif native_gain_step == 3 then
            event.new_lives = event.old_lives + 2
            pending_lives_change = tostring(event.old_lives) .. "->" .. tostring(event.new_lives)
            log("Native gain test replaced " .. transition .. " with " .. pending_lives_change)
            return
        end
    end
    if mutation_match and event.new_lives < event.old_lives and mutation_losses < 2 then
        mutation_losses = mutation_losses + 1
        if mutation_losses == 1 then
            event.cancelled = true
            pending_lives_change = nil
            log("Mutation match cancelled lives loss " .. transition)
            return
        end
        event.new_lives = event.old_lives + 1
        pending_lives_change = tostring(event.old_lives) .. "->" .. tostring(event.new_lives)
        log("Mutation match replaced lives loss " .. transition .. " with " .. pending_lives_change)
        return
    end
    if btd5.config.get("cancel_lives_loss") == "true" and
        event.new_lives < event.old_lives then
        event.cancelled = true
        pending_lives_change = nil
        log("Lifecycle Sample cancelled lives.changing " .. transition)
        return
    end
    if btd5.config.get("mutate_lives_loss") == "true" and
        event.new_lives < event.old_lives then
        event.new_lives = event.old_lives + 1
        local mutated = tostring(event.old_lives) .. "->" .. tostring(event.new_lives)
        pending_lives_change = mutated
        log("Lifecycle Sample mutated lives.changing " .. transition .. " to " .. mutated)
        return
    end
    pending_lives_change = transition
end)

btd5.events.on("lives.changed", function(event)
    assert(event.player:is_valid() and event.player:id() == pending_player_id)
    local completed = tostring(event.old_lives) .. "->" .. tostring(event.new_lives)
    assert(completed == pending_lives_change)
    pending_lives_change = nil
    pending_player_id = nil
    log("Lifecycle Sample observed lives.changed " .. completed)
end)

local pending_tower_placement
local gain_test_cash_granted = false

local tower_placement_cancelled = false

btd5.events.on("tower.placing", function(event)
    assert(event.tower:is_valid())
    assert(type(event.purchase) == "boolean")
    if event.purchase and not tower_placement_cancelled and
        enabled("cancel_tower_placement") then
        -- Reject only the first purchase so the next attempt proves recovery.
        tower_placement_cancelled = true
        event.cancelled = true
        log("Lifecycle Sample cancelled tower.placing id=" .. event.tower:id())
        return
    end
    pending_tower_placement = event.tower:id()
    log("Lifecycle Sample observed tower.placing id=" .. pending_tower_placement ..
        " purchase=" .. tostring(event.purchase))
end)

btd5.events.on("tower.placed", function(event)
    assert(event.tower:is_valid())
    assert(event.tower:id() == pending_tower_placement)
    retained_match_tower = event.tower
    log("Lifecycle Sample observed tower.placed id=" .. event.tower:id())
    if btd5.config.get("test_native_gains") == "true" and not gain_test_cash_granted then
        assert(event.tower:set_sell_price(100000))
        assert(event.tower:sell_price() == 100000)
        gain_test_cash_granted = true
        log("Native gain test funding ready: sell this first tower for bonus cash")
    end
    if enabled("mutate_tower_pop_count") then
        local before = event.tower:pop_count()
        assert(event.tower:set_pop_count(123))
        local after = event.tower:pop_count()
        assert(after == 123)
        log("Lifecycle Sample mutated tower.pop_count " .. before .. "->" .. after)
    end
    if enabled("mutate_direct_properties") then
        local before = event.tower:sell_price()
        assert(event.tower:set_sell_price(777))
        local after = event.tower:sell_price()
        assert(after == 777)
        log("Lifecycle Sample mutated tower.sell_price " .. before .. "->" .. after)
    end
    pending_tower_placement = nil
end)

local pending_tower_upgrade
local mutation_upgrade_cancelled = false
local mutation_sale_cancelled = false

btd5.events.on("tower.upgrading", function(event)
    assert(event.tower:is_valid())
    pending_tower_upgrade = event.tower:id()
    log("Lifecycle Sample observed tower.upgrading id=" .. pending_tower_upgrade)
    if btd5.config.get("cancel_tower_actions") == "true" or
        (mutation_match and not mutation_upgrade_cancelled) then
        mutation_upgrade_cancelled = true
        event.cancelled = true
        log("Lifecycle Sample cancelled tower.upgrading id=" .. pending_tower_upgrade)
        pending_tower_upgrade = nil
    end
end)

btd5.events.on("tower.upgraded", function(event)
    assert(event.tower:is_valid())
    assert(event.tower:id() == pending_tower_upgrade)
    log("Lifecycle Sample observed tower.upgraded id=" .. event.tower:id())
    pending_tower_upgrade = nil
end)

local pending_tower_sale

btd5.events.on("tower.selling", function(event)
    assert(event.tower:is_valid())
    pending_tower_sale = event.tower:id()
    log("Lifecycle Sample observed tower.selling id=" .. pending_tower_sale)
    if btd5.config.get("cancel_tower_actions") == "true" or
        (mutation_match and not mutation_sale_cancelled) then
        mutation_sale_cancelled = true
        event.cancelled = true
        log("Lifecycle Sample cancelled tower.selling id=" .. pending_tower_sale)
        pending_tower_sale = nil
    end
end)

btd5.events.on("tower.sold", function(event)
    local sold = event.tower
    assert(sold:is_valid())
    assert(sold:id() == pending_tower_sale)
    log("Lifecycle Sample observed tower.sold id=" .. sold:id())
    pending_tower_sale = nil
    btd5.timer.after(1, function()
        assert(not sold:is_valid())
        log("Lifecycle Sample confirmed sold tower became stale")
    end)
end)

local pending_bloon_spawns = {}
-- Spawns and pops are logged only for the first bloons of a match; leaks are
-- rare and are always logged. Child bloons released by a pop never pass
-- through bloon.spawning, so pops are not required to match a spawn.
local BLOON_LOG_LIMIT = 50
local logged_bloons = {}
local bloons_logged = 0

btd5.events.on("match.ended", function()
    logged_bloons = {}
    bloons_logged = 0
end)

-- Cancels a few round spawns. A cancelled spawn never joins the game: its
-- wrapper is stale once the handlers return, it never reaches bloon.spawned,
-- and the round still ends.
local cancel_bloon_spawns = btd5.config.get("cancel_bloon_spawns") == "true"
local SPAWN_CANCEL_LIMIT = 3
local cancelled_spawns = {}
local cancelled_spawn_count = 0
local round_spawns_seen = 0

btd5.events.on("round.ended", function()
    if cancelled_spawn_count > 0 then
        log("Lifecycle Sample round ended after cancelled spawns count=" .. cancelled_spawn_count ..
            " round_spawns=" .. round_spawns_seen)
    end
end)

btd5.events.on("bloon.spawning", function(event)
    assert(event.bloon:is_valid())
    local id = event.bloon:id()
    if event.from_round then
        round_spawns_seen = round_spawns_seen + 1
    end
    if cancel_bloon_spawns and event.from_round and cancelled_spawn_count < SPAWN_CANCEL_LIMIT then
        cancelled_spawn_count = cancelled_spawn_count + 1
        cancelled_spawns[id] = true
        local bloon = event.bloon
        event.cancelled = true
        log("Lifecycle Sample cancelled bloon.spawning id=" .. id)
        btd5.timer.after(1, function()
            assert(not bloon:is_valid())
            log("Lifecycle Sample confirmed cancelled spawn stale id=" .. id)
        end)
        return
    end
    -- The game reuses a freed bloon's memory, so a later bloon can carry a
    -- cancelled bloon's id; its own spawning starts a new life for that id.
    cancelled_spawns[id] = nil
    pending_bloon_spawns[id] = true
    if bloons_logged < BLOON_LOG_LIMIT then
        bloons_logged = bloons_logged + 1
        logged_bloons[id] = true
        log("Lifecycle Sample observed bloon.spawning id=" .. id)
    end
end)

btd5.events.on("bloon.spawned", function(event)
    assert(event.bloon:is_valid())
    local id = event.bloon:id()
    assert(pending_bloon_spawns[id])
    assert(not cancelled_spawns[id])
    pending_bloon_spawns[id] = nil
    retained_match_bloon = event.bloon
    if logged_bloons[id] then
        log("Lifecycle Sample observed bloon.spawned id=" .. id)
    end
    if enabled("mutate_direct_properties") and logged_bloons[id] then
        local before = event.bloon:health()
        local replacement = before + 1
        assert(event.bloon:set_health(replacement))
        local after = event.bloon:health()
        assert(after == replacement)
        log("Lifecycle Sample mutated bloon.health " .. before .. "->" .. after)
    end
end)

-- bloon.popping runs at the lethal hit and bloon.popped when the manager
-- pops the bloon on its next update, so several pops can be pending at once.
local pending_bloon_pops = {}
-- Cancels the first lethal hit on a few bloons; each must stay alive with its
-- pre-hit health and pop normally on a later hit.
local cancel_bloon_pops = btd5.config.get("cancel_bloon_pops") == "true"
local POP_CANCEL_LIMIT = 3
local cancelled_pops = {}
local cancelled_pop_count = 0

btd5.events.on("bloon.popping", function(event)
    local bloon = event.bloon
    assert(bloon:is_valid())
    local id = bloon:id()
    if cancel_bloon_pops and not cancelled_pops[id] and cancelled_pop_count < POP_CANCEL_LIMIT then
        cancelled_pop_count = cancelled_pop_count + 1
        cancelled_pops[id] = true
        local health = bloon:health()
        assert(health > 0)
        event.cancelled = true
        log("Lifecycle Sample cancelled bloon.popping id=" .. id .. " health=" .. health)
        btd5.timer.after(1, function()
            if bloon:is_valid() then
                assert(bloon:health() > 0)
                log("Lifecycle Sample confirmed cancelled pop kept bloon id=" .. id)
            end
        end)
        return
    end
    pending_bloon_pops[id] = true
    if logged_bloons[id] then
        log("Lifecycle Sample observed bloon.popping id=" .. id)
    end
end)

btd5.events.on("bloon.popped", function(event)
    local popped = event.bloon
    assert(popped:is_valid())
    local id = popped:id()
    assert(pending_bloon_pops[id])
    pending_bloon_pops[id] = nil
    if cancelled_pops[id] then
        log("Lifecycle Sample popped previously cancelled bloon id=" .. id)
    end
    local logged = logged_bloons[id]
    logged_bloons[id] = nil
    btd5.timer.after(1, function()
        assert(not popped:is_valid())
        if logged then
            log("Lifecycle Sample confirmed popped bloon became stale")
        end
    end)
    if logged then
        log("Lifecycle Sample observed bloon.popped id=" .. id)
    end
end)

local pending_bloon_leak
local checked_cancelled_bloons = {}
-- The mutation match cancels one leak attempt; the game retries it on the
-- next frame, and that attempt must leak the same bloon normally.
local mutation_leak_cancelled

btd5.events.on("bloon.leaking", function(event)
    assert(event.bloon:is_valid())
    if btd5.config.get("cancel_bloon_leaks") == "true" then
        local bloon = event.bloon
        event.cancelled = true
        if not checked_cancelled_bloons[bloon:id()] then
            checked_cancelled_bloons[bloon:id()] = true
            log("Lifecycle Sample cancelled bloon.leaking id=" .. bloon:id())
            btd5.timer.after(2, function()
                assert(bloon:is_valid())
                log("Lifecycle Sample confirmed cancelled bloon remains valid id=" .. bloon:id())
            end)
        end
        return
    end
    if mutation_match and not mutation_leak_cancelled then
        mutation_leak_cancelled = event.bloon:id()
        event.cancelled = true
        log("Lifecycle Sample cancelled bloon.leaking id=" .. mutation_leak_cancelled)
        return
    end
    pending_bloon_leak = event.bloon:id()
    log("Lifecycle Sample observed bloon.leaking id=" .. pending_bloon_leak)
end)

btd5.events.on("bloon.leaked", function(event)
    local leaked = event.bloon
    assert(leaked:is_valid())
    assert(leaked:id() == pending_bloon_leak)
    logged_bloons[leaked:id()] = nil
    log("Lifecycle Sample observed bloon.leaked id=" .. leaked:id())
    if mutation_match and leaked:id() == mutation_leak_cancelled then
        log("Mutation match cancelled bloon leaked after continuing id=" .. leaked:id())
    end
    pending_bloon_leak = nil
    btd5.timer.after(1, function()
        assert(not leaked:is_valid())
        log("Lifecycle Sample confirmed leaked bloon became stale")
    end)
end)

if btd5.config.get("observe_attacks_projectiles") == "true" then
    -- Attack and projectile lifetimes are high volume: track every wrapper but
    -- log only the first few lifetimes of each kind.
    local function observe_lifetime(kind)
        local live = {}
        local logged = 0
        local stale_confirmed = false
        btd5.events.on(kind .. ".created", function(event)
            local object = event[kind]
            assert(object:is_valid())
            assert(object:kind() == kind)
            local id = object:id()
            assert(live[id] == nil)
            live[id] = true
            if logged < 3 then
                log("Lifecycle Sample observed " .. kind .. ".created id=" .. id)
            end
        end)
        btd5.events.on(kind .. ".destroyed", function(event)
            local object = event[kind]
            assert(object:is_valid())
            local id = object:id()
            assert(live[id])
            live[id] = nil
            if logged < 3 then
                logged = logged + 1
                log("Lifecycle Sample observed " .. kind .. ".destroyed id=" .. id)
            end
            if not stale_confirmed then
                stale_confirmed = true
                btd5.timer.after(1, function()
                    assert(not object:is_valid())
                    log("Lifecycle Sample confirmed destroyed " .. kind .. " became stale")
                end)
            end
        end)
        btd5.events.on("match.ended", function()
            live = {}
        end)
    end
    observe_lifetime("attack")
    observe_lifetime("projectile")
end

-- Identity getters and match:set_cash (opt-in): logs each tower type and its
-- upgrade tiers, every round number, each distinct bloon type, and one cash
-- raise and restore through the native money helpers.
if btd5.config.get("test_identity_cash") == "true" then
    local function log_tiers(label, tower)
        local left, right = tower:upgrade_tiers()
        log("Identity test " .. label .. " id=" .. tower:id() .. " type=" ..
            tostring(tower:type()) .. " tiers=" .. left .. "," .. right)
    end
    btd5.events.on("tower.placed", function(event) log_tiers("tower.placed", event.tower) end)
    btd5.events.on("tower.upgraded", function(event) log_tiers("tower.upgraded", event.tower) end)

    local seen_bloon_types = {}
    btd5.events.on("bloon.spawned", function(event)
        local name = tostring(event.bloon:type())
        if not seen_bloon_types[name] then
            seen_bloon_types[name] = true
            log("Identity test bloon type=" .. name)
        end
    end)

    local last_cash
    local cash_target
    btd5.events.on("cash.changed", function(event)
        last_cash = event.cash
        if cash_target and event.cash == cash_target then
            log("Identity test observed cash.changed cash=" .. string.format("%.0f", event.cash))
            cash_target = nil
        end
    end)

    local cash_tested = false
    btd5.events.on("round.started", function(event)
        log("Identity test round number=" .. event.round:number())
        if cash_tested or not last_cash or not retained_match:is_valid() then return end
        cash_tested = true
        local before = last_cash
        cash_target = before + 500
        assert(retained_match:set_cash(before + 500))
        assert(last_cash == before + 500)
        cash_target = before
        assert(retained_match:set_cash(before))
        assert(last_cash == before)
        log("Identity test set match cash " .. string.format("%.0f", before) .. "->" ..
            string.format("%.0f", before + 500) .. "->" .. string.format("%.0f", before))
    end)
end

function on_load()
    local launches = tonumber(btd5.storage.get("launches") or "0") + 1
    btd5.storage.set("launches", tostring(launches))
    log(btd5.config.get("greeting") .. " (launch " .. launches .. ")")
end

function on_ready()
    log(btd5.localization.get("sample.lifecycle.ready"))
    btd5.timer.after(60, function() log("deterministic timer fired") end)
end

function on_shutdown()
    log("Lifecycle Sample shut down cleanly")
end
