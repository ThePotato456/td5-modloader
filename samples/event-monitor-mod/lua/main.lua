local counters = {
    towers_placed = 0,
    towers_upgraded = 0,
    towers_sold = 0,
    bloons_spawned = 0,
    bloons_popped = 0,
    bloons_leaked = 0
}

local subscriptions = {}
local log_each_action = false

local function subscribe(name, callback)
    subscriptions[#subscriptions + 1] = btd5.events.on(name, callback)
end

local function observe_object(event_name, object, counter_name)
    counters[counter_name] = counters[counter_name] + 1
    if log_each_action and object:is_valid() then
        btd5.log("info", event_name .. " " .. object:kind() .. ":" .. object:id())
    end
end

local function summary(prefix)
    btd5.log(
        "info",
        prefix ..
        " towers=" .. counters.towers_placed .. "/" .. counters.towers_upgraded .. "/" .. counters.towers_sold ..
        " bloons=" .. counters.bloons_spawned .. "/" .. counters.bloons_popped .. "/" .. counters.bloons_leaked
    )
end

subscribe("tower.placed", function(event)
    observe_object(event.name, event.tower, "towers_placed")
end)

subscribe("tower.upgraded", function(event)
    observe_object(event.name, event.tower, "towers_upgraded")
end)

subscribe("tower.sold", function(event)
    observe_object(event.name, event.tower, "towers_sold")
end)

subscribe("bloon.spawned", function(event)
    observe_object(event.name, event.bloon, "bloons_spawned")
end)

subscribe("bloon.popped", function(event)
    observe_object(event.name, event.bloon, "bloons_popped")
end)

subscribe("bloon.leaked", function(event)
    observe_object(event.name, event.bloon, "bloons_leaked")
end)

subscribe("round.ended", function()
    summary("Round summary:")
end)

subscribe("match.ended", function()
    local completed = tonumber(btd5.storage.get("completed_matches") or "0") + 1
    btd5.storage.set("completed_matches", tostring(completed))
    summary("Match summary:")
    btd5.log("info", "Observed completed matches=" .. completed)
end)

function on_load()
    log_each_action = btd5.config.get("log_each_action") == "true"
    btd5.log("info", "Event Monitor loaded")
end

function on_shutdown()
    for _, token in ipairs(subscriptions) do
        btd5.events.off(token)
    end
    btd5.log("info", "Event Monitor shut down")
end
