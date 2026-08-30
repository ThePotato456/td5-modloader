local function log(message)
    btd5.log("info", message)
end

btd5.events.on("match.starting", function()
    log("Lifecycle Sample observed match.starting")
end)

btd5.events.on("match.started", function()
    log("Lifecycle Sample observed match.started")
end)

btd5.events.on("match.ending", function()
    log("Lifecycle Sample observed match.ending")
end)

btd5.events.on("match.ended", function()
    log("Lifecycle Sample observed match.ended")
end)

btd5.events.on("round.starting", function()
    log("Lifecycle Sample observed round.starting")
end)

btd5.events.on("round.started", function()
    log("Lifecycle Sample observed round.started")
end)

btd5.events.on("round.ending", function()
    log("Lifecycle Sample observed round.ending")
end)

btd5.events.on("round.ended", function()
    log("Lifecycle Sample observed round.ended")
end)

btd5.events.on("cash.changing", function()
    log("Lifecycle Sample observed cash.changing")
end)

btd5.events.on("cash.changed", function()
    log("Lifecycle Sample observed cash.changed")
end)

local pending_lives_change

btd5.events.on("lives.changing", function(event)
    assert(event.old_lives ~= event.new_lives)
    local transition = tostring(event.old_lives) .. "->" .. tostring(event.new_lives)
    log("Lifecycle Sample observed lives.changing " .. transition)
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
    local completed = tostring(event.old_lives) .. "->" .. tostring(event.new_lives)
    assert(completed == pending_lives_change)
    pending_lives_change = nil
    log("Lifecycle Sample observed lives.changed " .. completed)
end)

local pending_tower_placement

btd5.events.on("tower.placing", function(event)
    assert(event.tower:is_valid())
    pending_tower_placement = event.tower:id()
    log("Lifecycle Sample observed tower.placing id=" .. pending_tower_placement)
end)

btd5.events.on("tower.placed", function(event)
    assert(event.tower:is_valid())
    assert(event.tower:id() == pending_tower_placement)
    log("Lifecycle Sample observed tower.placed id=" .. event.tower:id())
    if btd5.config.get("mutate_tower_pop_count") == "true" then
        local before = event.tower:pop_count()
        assert(event.tower:set_pop_count(123))
        local after = event.tower:pop_count()
        assert(after == 123)
        log("Lifecycle Sample mutated tower.pop_count " .. before .. "->" .. after)
    end
    if btd5.config.get("mutate_direct_properties") == "true" then
        local before = event.tower:sell_price()
        assert(event.tower:set_sell_price(777))
        local after = event.tower:sell_price()
        assert(after == 777)
        log("Lifecycle Sample mutated tower.sell_price " .. before .. "->" .. after)
    end
    pending_tower_placement = nil
end)

local pending_tower_upgrade

btd5.events.on("tower.upgrading", function(event)
    assert(event.tower:is_valid())
    pending_tower_upgrade = event.tower:id()
    log("Lifecycle Sample observed tower.upgrading id=" .. pending_tower_upgrade)
    if btd5.config.get("cancel_tower_actions") == "true" then
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
    if btd5.config.get("cancel_tower_actions") == "true" then
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

btd5.events.on("bloon.spawning", function(event)
    assert(event.bloon:is_valid())
    local id = event.bloon:id()
    pending_bloon_spawns[id] = true
    log("Lifecycle Sample observed bloon.spawning id=" .. id)
end)

btd5.events.on("bloon.spawned", function(event)
    assert(event.bloon:is_valid())
    local id = event.bloon:id()
    assert(pending_bloon_spawns[id])
    pending_bloon_spawns[id] = nil
    log("Lifecycle Sample observed bloon.spawned id=" .. id)
    if btd5.config.get("mutate_direct_properties") == "true" then
        local before = event.bloon:health()
        local replacement = before + 1
        assert(event.bloon:set_health(replacement))
        local after = event.bloon:health()
        assert(after == replacement)
        log("Lifecycle Sample mutated bloon.health " .. before .. "->" .. after)
    end
end)

local pending_bloon_pop

btd5.events.on("bloon.popping", function(event)
    assert(event.bloon:is_valid())
    pending_bloon_pop = event.bloon:id()
    log("Lifecycle Sample observed bloon.popping id=" .. pending_bloon_pop)
end)

btd5.events.on("bloon.popped", function(event)
    local popped = event.bloon
    assert(popped:is_valid())
    assert(popped:id() == pending_bloon_pop)
    log("Lifecycle Sample observed bloon.popped id=" .. popped:id())
    pending_bloon_pop = nil
    btd5.timer.after(1, function()
        assert(not popped:is_valid())
        log("Lifecycle Sample confirmed popped bloon became stale")
    end)
end)

local pending_bloon_leak

btd5.events.on("bloon.leaking", function(event)
    assert(event.bloon:is_valid())
    pending_bloon_leak = event.bloon:id()
    log("Lifecycle Sample observed bloon.leaking id=" .. pending_bloon_leak)
end)

btd5.events.on("bloon.leaked", function(event)
    local leaked = event.bloon
    assert(leaked:is_valid())
    assert(leaked:id() == pending_bloon_leak)
    log("Lifecycle Sample observed bloon.leaked id=" .. leaked:id())
    pending_bloon_leak = nil
    btd5.timer.after(1, function()
        assert(not leaked:is_valid())
        log("Lifecycle Sample confirmed leaked bloon became stale")
    end)
end)

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
