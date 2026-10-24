local show_note = true
local delay_frames = 60
local launches = 0

function on_load()
    show_note = btd5.config.get("show_note") ~= "false"
    local delay = tonumber(btd5.config.get("delay_frames") or "60") or 60
    delay_frames = math.floor(math.max(0, math.min(600, delay)))
    launches = (tonumber(btd5.storage.get("launches") or "0") or 0) + 1
    -- Save immediately: shutdown may not run after a crash or forced termination.
    btd5.storage.set("launches", tostring(launches))
    btd5.log("info", "[Resource Notes] " .. btd5.localization.get("notes.launches") ..
        "=" .. launches)
end

function on_ready()
    btd5.log("info", "[Resource Notes] " .. btd5.localization.get("notes.ready"))
    if not show_note then return end
    btd5.timer.after(delay_frames, function()
        local note = btd5.resource.read_text("assets/note.txt")
        btd5.log("info", "[Resource Notes] " .. note)
    end)
end

function on_shutdown()
    btd5.log("info", "[Resource Notes] shutdown; counter was already saved")
end
