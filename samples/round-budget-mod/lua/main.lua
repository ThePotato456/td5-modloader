-- Set an absolute budget once per round; never write cash from a cash handler.
local match
local enabled = false
local round_cash = 1000
local last_cash

btd5.events.on("match.started", function(event)
    match = event.match
    last_cash = nil
end)

btd5.events.on("cash.changed", function(event)
    -- Cash events are read-only notifications, including synchronous setter results.
    last_cash = event.cash
end)

btd5.events.on("round.started", function(event)
    if not enabled or not match or not match:is_valid() then return end
    -- set_cash dispatches cash events before it returns, so last_cash updates here.
    match:set_cash(round_cash)
    btd5.log("info", "[Round Budget] round=" .. event.round:number() ..
        " target=" .. round_cash .. " observed=" .. tostring(last_cash))
end)

btd5.events.on("match.ended", function()
    -- match.ended's own wrapper is already stale; release our retained reference.
    match = nil
    last_cash = nil
end)

function on_load()
    enabled = btd5.config.get("enabled") == "true"
    local value = tonumber(btd5.config.get("round_cash") or "1000") or 1000
    round_cash = math.floor(math.max(0, math.min(2147483647, value)))
    btd5.log("info", "[Round Budget] loaded; enabled=" .. tostring(enabled))
end
