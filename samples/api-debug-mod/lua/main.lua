local event_names = {
    "match.starting", "match.started", "match.ending", "match.ended",
    "round.starting", "round.started", "round.ending", "round.ended",
    "cash.changing", "cash.changed", "lives.changing", "lives.changed",
    "tower.placing", "tower.placed", "tower.upgrading", "tower.upgraded",
    "tower.selling", "tower.sold", "bloon.spawning", "bloon.spawned",
    "bloon.popping", "bloon.popped", "bloon.leaking", "bloon.leaked"
}

local subscriptions = {}
local event_counts = {}
local detail_counts = {}
local action_counts = {}
local event_count = 0
local terminal_checks = 0

-- Defaults are deliberately bounded. Configuration is read once during
-- on_load so high-frequency callbacks do not repeatedly cross the host API.
local settings = {
    log_event_details = false,
    detailed_event_limit = 2,
    log_round_summaries = true,
    log_object_properties = false,
    verify_terminal_wrappers = false,
    terminal_wrapper_limit = 3,
    operation_limit = 1,
    mutate_tower_pop_count = false,
    tower_pop_count = 123,
    mutate_tower_sell_price = false,
    tower_sell_price = 777,
    mutate_bloon_health = false,
    bloon_health_bonus = 1.0,
    replace_lives_below_minimum = false,
    minimum_lives = 1,
    cancel_lives_losses = false,
    cancel_tower_upgrades = false,
    cancel_tower_sales = false,
    cancel_bloon_leaks = false
}

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

local function bounded_integer(name, fallback, minimum, maximum)
    return math.max(minimum, math.min(maximum, config_integer(name, fallback)))
end

local function load_settings()
    settings.log_event_details = config_bool("log_event_details", false)
    settings.detailed_event_limit = bounded_integer("detailed_event_limit", 2, 0, 20)
    settings.log_round_summaries = config_bool("log_round_summaries", true)
    settings.log_object_properties = config_bool("log_object_properties", false)
    settings.verify_terminal_wrappers = config_bool("verify_terminal_wrappers", false)
    settings.terminal_wrapper_limit = bounded_integer("terminal_wrapper_limit", 3, 0, 20)
    settings.operation_limit = bounded_integer("operation_limit", 1, 0, 20)
    settings.mutate_tower_pop_count = config_bool("mutate_tower_pop_count", false)
    settings.tower_pop_count = bounded_integer("tower_pop_count", 123, 0, 2147483647)
    settings.mutate_tower_sell_price = config_bool("mutate_tower_sell_price", false)
    settings.tower_sell_price = bounded_integer("tower_sell_price", 777, 0, 2147483647)
    settings.mutate_bloon_health = config_bool("mutate_bloon_health", false)
    settings.bloon_health_bonus = math.max(
        -1000000.0,
        math.min(1000000.0, config_number("bloon_health_bonus", 1.0))
    )
    settings.replace_lives_below_minimum =
        config_bool("replace_lives_below_minimum", false)
    settings.minimum_lives = bounded_integer("minimum_lives", 1, 0, 2147483647)
    settings.cancel_lives_losses = config_bool("cancel_lives_losses", false)
    settings.cancel_tower_upgrades = config_bool("cancel_tower_upgrades", false)
    settings.cancel_tower_sales = config_bool("cancel_tower_sales", false)
    settings.cancel_bloon_leaks = config_bool("cancel_bloon_leaks", false)
end

local function subscribe(name, callback)
    subscriptions[#subscriptions + 1] = btd5.events.on(name, callback)
end

local function take_action(name, enabled)
    if not enabled then
        return false
    end
    local count = action_counts[name] or 0
    if count >= settings.operation_limit then
        return false
    end
    action_counts[name] = count + 1
    return true
end

local function describe_object(object)
    if not object:is_valid() then
        return "stale object"
    end
    local kind = object:kind()
    local description = kind .. ":" .. object:id()
    if settings.log_object_properties then
        if kind == "tower" then
            description = description ..
                " pop_count=" .. object:pop_count() ..
                " sell_price=" .. object:sell_price()
        elseif kind == "bloon" then
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

local function maybe_log_event(event)
    if not settings.log_event_details then
        return
    end
    local count = detail_counts[event.name] or 0
    if count >= settings.detailed_event_limit then
        return
    end
    detail_counts[event.name] = count + 1
    log("event #" .. event_count .. " " .. describe_event(event))
end

for _, name in ipairs(event_names) do
    subscribe(name, function(event)
        event_count = event_count + 1
        event_counts[event.name] = (event_counts[event.name] or 0) + 1
        maybe_log_event(event)
    end)
end

subscribe("lives.changing", function(event)
    if event.new_lives >= event.old_lives then
        return
    end
    if take_action("cancel_lives_losses", settings.cancel_lives_losses) then
        event.cancelled = true
        log("cancelled one lives loss " .. event.old_lives .. "->" .. event.new_lives)
        return
    end
    if event.new_lives < settings.minimum_lives and take_action(
        "replace_lives_below_minimum",
        settings.replace_lives_below_minimum
    ) then
        local proposed = event.new_lives
        event.new_lives = settings.minimum_lives
        log("replaced one lives loss " .. proposed .. " with " .. event.new_lives)
    end
end)

subscribe("tower.placed", function(event)
    if take_action("mutate_tower_pop_count", settings.mutate_tower_pop_count) then
        local before = event.tower:pop_count()
        event.tower:set_pop_count(settings.tower_pop_count)
        log("tower pop_count " .. before .. "->" .. event.tower:pop_count())
    end
    if take_action("mutate_tower_sell_price", settings.mutate_tower_sell_price) then
        local before = event.tower:sell_price()
        event.tower:set_sell_price(settings.tower_sell_price)
        log("tower sell_price " .. before .. "->" .. event.tower:sell_price())
    end
end)

subscribe("tower.upgrading", function(event)
    if take_action("cancel_tower_upgrades", settings.cancel_tower_upgrades) then
        event.cancelled = true
        log("cancelled one upgrade for " .. describe_object(event.tower))
    end
end)

subscribe("tower.selling", function(event)
    if take_action("cancel_tower_sales", settings.cancel_tower_sales) then
        event.cancelled = true
        log("cancelled one sale for " .. describe_object(event.tower))
    end
end)

subscribe("bloon.spawned", function(event)
    if take_action("mutate_bloon_health", settings.mutate_bloon_health) then
        local before = event.bloon:health()
        local replacement = math.max(0, before + settings.bloon_health_bonus)
        event.bloon:set_health(replacement)
        log("bloon health " .. before .. "->" .. event.bloon:health())
    end
end)

subscribe("bloon.leaking", function(event)
    -- This is globally bounded, not once per bloon. A repeatedly cancelled leak
    -- would otherwise keep the bloon alive and re-enter this hook indefinitely.
    if take_action("cancel_bloon_leaks", settings.cancel_bloon_leaks) then
        event.cancelled = true
        log("cancelled one leak for " .. describe_object(event.bloon))
    end
end)

local function verify_terminal_wrapper(label, object)
    if not settings.verify_terminal_wrappers or
        terminal_checks >= settings.terminal_wrapper_limit then
        return
    end
    terminal_checks = terminal_checks + 1
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

local function count(name)
    return event_counts[name] or 0
end

subscribe("round.ended", function()
    if settings.log_round_summaries then
        log(
            "round summary events=" .. event_count ..
            " towers=" .. count("tower.placed") .. "/" ..
                count("tower.upgraded") .. "/" .. count("tower.sold") ..
            " bloons=" .. count("bloon.spawned") .. "/" ..
                count("bloon.popped") .. "/" .. count("bloon.leaked")
        )
    end
end)

subscribe("match.ended", function()
    local matches = tonumber(btd5.storage.get("completed_matches") or "0") + 1
    btd5.storage.set("completed_matches", tostring(matches))
    btd5.storage.set("last_match_event_count", tostring(event_count))
    log("stored completed_matches=" .. matches .. " events=" .. event_count)
end)

function on_load()
    load_settings()
    local launches = tonumber(btd5.storage.get("launches") or "0") + 1
    btd5.storage.set("launches", tostring(launches))
    local resource = btd5.resource.read_text("assets/about.txt")
    log("loaded launch=" .. launches .. " operation_limit=" .. settings.operation_limit)
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
