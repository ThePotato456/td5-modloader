function on_load()
    btd5.log("info", "Hello World loaded")
end

function on_ready()
    btd5.log("info", "Hello World is ready")
    btd5.timer.after(60, function()
        btd5.log("info", "Hello World timer fired after 60 rendered frames")
    end)
end

function on_shutdown()
    btd5.log("info", "Hello World shut down")
end
