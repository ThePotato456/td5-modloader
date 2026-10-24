-- Each operation is globally bounded to once per match, including leak retries.
local used = {}

local function once(key)
    if used[key] or btd5.config.get(key) ~= "true" then return false end
    used[key] = true -- Set before any API that could re-enter a callback.
    return true
end

btd5.events.on("match.started", function() used = {} end)

btd5.events.on("bloon.spawning", function(event)
    if event.from_round and once("cancel_first_spawn") then
        event.cancelled = true
        btd5.log("info", "[Bloon Rules] cancelled one round spawn")
    end
end)

btd5.events.on("bloon.spawned", function(event)
    if not once("boost_first_health") then return end
    local bloon = event.bloon
    local bonus = tonumber(btd5.config.get("health_bonus") or "1") or 1
    bonus = math.max(0, math.min(1000, bonus))
    bloon:set_health(math.min(3.4028234663852886e38, bloon:health() + bonus))
    btd5.log("info", "[Bloon Rules] boosted " .. tostring(bloon:type()) ..
        " health=" .. bloon:health())
end)

btd5.events.on("bloon.popping", function(event)
    if once("cancel_first_pop") then
        event.cancelled = true
        btd5.log("info", "[Bloon Rules] cancelled one lethal hit; health=" ..
            event.bloon:health())
    end
end)

btd5.events.on("bloon.leaking", function(event)
    if once("cancel_first_leak") then
        event.cancelled = true
        btd5.log("info", "[Bloon Rules] cancelled one leak attempt")
    end
end)
