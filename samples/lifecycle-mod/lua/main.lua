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
