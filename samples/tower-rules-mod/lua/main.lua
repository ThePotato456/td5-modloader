local blocked = {}

local function flag(key)
    return btd5.config.get(key) == "true"
end

local function integer_setting(key, fallback)
    local value = tonumber(btd5.config.get(key) or tostring(fallback)) or fallback
    return math.floor(math.max(0, math.min(2147483647, value)))
end

local function reject_once(key, event)
    if flag(key) and not blocked[key] then
        blocked[key] = true
        event.cancelled = true
        btd5.log("info", "[Tower Rules] rejected " .. event.name)
    end
end

btd5.events.on("match.started", function() blocked = {} end)

btd5.events.on("tower.placing", function(event)
    -- Game-created towers are not cancellable purchases.
    if event.purchase then reject_once("block_first_purchase", event) end
end)

btd5.events.on("tower.placed", function(event)
    local tower = event.tower
    local left, right = tower:upgrade_tiers()
    btd5.log("info", "[Tower Rules] placed " .. tostring(tower:type()) ..
        " tiers=" .. left .. "," .. right)
    if flag("override_properties") then
        tower:set_pop_count(integer_setting("pop_count", 123))
        tower:set_sell_price(integer_setting("sell_price", 777))
        btd5.log("info", "[Tower Rules] pops=" .. tower:pop_count() ..
            " base_sell_price=" .. tower:sell_price())
    end
end)

btd5.events.on("tower.upgrading", function(event)
    reject_once("block_first_upgrade", event)
end)

btd5.events.on("tower.selling", function(event)
    reject_once("block_first_sale", event)
end)

btd5.events.on("tower.sold", function(event)
    local tower = event.tower
    local id = tower:id() -- Capture primitives while the wrapper is valid.
    btd5.timer.after(1, function()
        btd5.log("info", "[Tower Rules] sold id=" .. id ..
            " stale=" .. tostring(not tower:is_valid()))
    end)
end)
