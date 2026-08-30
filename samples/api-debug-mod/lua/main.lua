local event_names = {
    "match.starting", "match.started", "match.ending", "match.ended",
    "round.starting", "round.started", "round.ending", "round.ended",
    "cash.changing", "cash.changed", "lives.changing", "lives.changed",
    "tower.placing", "tower.placed", "tower.upgrading", "tower.upgraded",
    "tower.selling", "tower.sold", "bloon.spawning", "bloon.spawned",
    "bloon.popping", "bloon.popped", "bloon.leaking", "bloon.leaked"
}

local subscriptions = {}
local event_count = 0

local function log(message)
    btd5.log("info", "[API Debug] " .. message)
end

local function config_bool(name, fallback)
    local value = btd5.config.get(name)
    if value == nil then
        return fallback
    end
    return value == "true"
end

local function config_number(name, fallback)
    return tonumber(btd5.config.get(name)) or fallback
end

local function config_integer(name, fallback)
    return math.tointeger(config_number(name, fallback)) or fallback
end

local function subscribe(name, callback)
    subscriptions[#subscriptions + 1] = btd5.events.on(name, callback)
end

local function describe_object(object)
    if not object:is_valid() then
        return "stale object"
    end
    local description = object:kind() .. ":" .. object:id()
    if config_bool("log_object_properties", true) then
        if object:kind() == "tower" then
            description = description ..
                " pop_count=" .. object:pop_count() ..
                " sell_price=" .. object:sell_price()
        elseif object:kind() == "bloon" then
            description = description .. " health=" .. object:health()
        end
    end
    return description
end

local function describe_event(event)
    local description = event.name
    if event.old_lives ~= nil then
        description = description .. " lives=" .. event.old_lives .. "->" .. event.new_lives
    elseif event.tower ~= nil then
        description = description .. " " .. describe_object(event.tower)
    elseif event.bloon ~= nil then
        description = description .. " " .. describe_object(event.bloon)
    end
    return description
end

for _, name in ipairs(event_names) do
    subscribe(name, function(event)
        event_count = event_count + 1
        if config_bool("log_every_event", true) then
            log("event #" .. event_count .. " " .. describe_event(event))
        end
    end)
end

subscribe("lives.changing", function(event)
    if event.new_lives >= event.old_lives then
        return
    end
    if config_bool("cancel_lives_losses", false) then
        event.cancelled = true
        log("cancelled lives loss " .. event.old_lives .. "->" .. event.new_lives)
        return
    end
    local minimum = math.max(0, config_integer("minimum_lives", 1))
    if config_bool("replace_lives_below_minimum", false) and event.new_lives < minimum then
        local proposed = event.new_lives
        event.new_lives = minimum
        log("replaced lives loss " .. proposed .. " with " .. event.new_lives)
    end
end)

subscribe("tower.placed", function(event)
    if config_bool("mutate_tower_pop_count", false) then
        local before = event.tower:pop_count()
        local replacement = math.max(0, config_integer("tower_pop_count", 123))
        event.tower:set_pop_count(replacement)
        log("tower pop_count " .. before .. "->" .. event.tower:pop_count())
    end
    if config_bool("mutate_tower_sell_price", false) then
        local before = event.tower:sell_price()
        local replacement = math.max(0, config_integer("tower_sell_price", 777))
        event.tower:set_sell_price(replacement)
        log("tower sell_price " .. before .. "->" .. event.tower:sell_price())
    end
end)

subscribe("tower.upgrading", function(event)
    if config_bool("cancel_tower_upgrades", false) then
        event.cancelled = true
        log("cancelled upgrade for " .. describe_object(event.tower))
    end
end)

subscribe("tower.selling", function(event)
    if config_bool("cancel_tower_sales", false) then
        event.cancelled = true
        log("cancelled sale for " .. describe_object(event.tower))
    end
end)

subscribe("bloon.spawned", function(event)
    if config_bool("mutate_bloon_health", false) then
        local before = event.bloon:health()
        local replacement = math.max(
            0,
            before + config_number("bloon_health_bonus", 1.0)
        )
        event.bloon:set_health(replacement)
        log("bloon health " .. before .. "->" .. event.bloon:health())
    end
end)

subscribe("bloon.leaking", function(event)
    if config_bool("cancel_bloon_leaks", false) then
        event.cancelled = true
        log("cancelled leak for " .. describe_object(event.bloon))
    end
end)

local function verify_terminal_wrapper(label, object)
    btd5.timer.after(1, function()
        assert(not object:is_valid())
        log(label .. " wrapper became stale as expected")
    end)
end

subscribe("tower.sold", function(event)
    verify_terminal_wrapper("sold tower", event.tower)
end)

subscribe("bloon.popped", function(event)
    verify_terminal_wrapper("popped bloon", event.bloon)
end)

subscribe("bloon.leaked", function(event)
    verify_terminal_wrapper("leaked bloon", event.bloon)
end)

subscribe("match.ended", function()
    local matches = tonumber(btd5.storage.get("completed_matches") or "0") + 1
    btd5.storage.set("completed_matches", tostring(matches))
    btd5.storage.set("last_match_event_count", tostring(event_count))
    log("stored completed_matches=" .. matches .. " events=" .. event_count)
end)

function on_load()
    local launches = tonumber(btd5.storage.get("launches") or "0") + 1
    btd5.storage.set("launches", tostring(launches))
    local resource = btd5.resource.read_text("resources/about.txt")
    log("loaded launch=" .. launches)
    log(resource)
end

function on_ready()
    log(btd5.localization.get("example.api-debug.ready"))
    btd5.timer.after(60, function()
        log("60-frame timer fired")
    end)
end

function on_shutdown()
    btd5.storage.set("last_session_event_count", tostring(event_count))
    for _, token in ipairs(subscriptions) do
        btd5.events.off(token)
    end
    log("shut down after " .. event_count .. " events")
end
