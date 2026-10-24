local enabled = true
local minimum_lives = 1

btd5.events.on("lives.changing", function(event)
    if not enabled or event.new_lives >= event.old_lives then
        return
    end

    if event.new_lives < minimum_lives then
        event.cancelled = true
        btd5.log(
            "info",
            "Lives Guardian cancelled " .. event.old_lives .. " -> " .. event.new_lives
        )
    end
end)

btd5.events.on("lives.changed", function(event)
    btd5.log("info", "Lives changed " .. event.old_lives .. " -> " .. event.new_lives)
end)

function on_load()
    enabled = btd5.config.get("enabled") ~= "false"
    minimum_lives = tonumber(btd5.config.get("minimum_lives") or "1") or 1
    minimum_lives = math.max(0, math.floor(minimum_lives))
    btd5.log("info", "Lives Guardian loaded with minimum_lives=" .. minimum_lives)
end
