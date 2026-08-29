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

btd5.events.on("lives.changed", function()
    log("Lifecycle Sample observed lives.changed")
end)

btd5.events.on("tower.placed", function(event)
    assert(event.tower:is_valid())
    log("Lifecycle Sample observed tower.placed id=" .. event.tower:id())
end)

btd5.events.on("tower.upgraded", function(event)
    assert(event.tower:is_valid())
    log("Lifecycle Sample observed tower.upgraded id=" .. event.tower:id())
end)

btd5.events.on("tower.sold", function(event)
    local sold = event.tower
    assert(sold:is_valid())
    log("Lifecycle Sample observed tower.sold id=" .. sold:id())
    btd5.timer.after(1, function()
        assert(not sold:is_valid())
        log("Lifecycle Sample confirmed sold tower became stale")
    end)
end)

btd5.events.on("bloon.spawned", function(event)
    assert(event.bloon:is_valid())
    log("Lifecycle Sample observed bloon.spawned id=" .. event.bloon:id())
end)

btd5.events.on("bloon.popped", function(event)
    local popped = event.bloon
    assert(popped:is_valid())
    log("Lifecycle Sample observed bloon.popped id=" .. popped:id())
    btd5.timer.after(1, function()
        assert(not popped:is_valid())
        log("Lifecycle Sample confirmed popped bloon became stale")
    end)
end)

btd5.events.on("bloon.leaked", function(event)
    local leaked = event.bloon
    assert(leaked:is_valid())
    log("Lifecycle Sample observed bloon.leaked id=" .. leaked:id())
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
