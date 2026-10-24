// SPDX-License-Identifier: GPL-3.0-only
#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <thread>

#include <lauxlib.h>
#include <lua.h>

#include "../../src/native/runtime/lua_mod.hpp"

using namespace btd5loader::runtime;

TEST_CASE("nested lifecycle work cannot reset or remove the outer budget", "[.boundaries]") {
    LuaMod* active = nullptr;
    bool inner_succeeded = false;
    LuaModOptions options;
    options.mod_id = "test.nested-budget";
    options.instruction_budget = 2'000;
    options.log = [&](auto, auto message) {
        if (message == "nested") inner_succeeded = active->invoke("on_ready");
    };
    LuaMod mod(options);
    active = &mod;
    REQUIRE(mod.load_script(R"(
        function on_ready() end
        function on_load()
            btd5.log("info", "nested")
            for i = 1, 100000 do end
        end
        function on_shutdown() for i = 1, 100000 do end end
    )", "nested-control.lua"));
    REQUIRE_FALSE(mod.invoke("on_load"));
    REQUIRE(inner_succeeded);
    REQUIRE(mod.last_error().find("CPU instruction budget") != std::string_view::npos);
    REQUIRE_FALSE(mod.invoke("on_shutdown"));
    REQUIRE(mod.invoke("on_ready"));

    LuaMod short_calls(options);
    active = &short_calls;
    REQUIRE(short_calls.load_script(R"(
        function on_ready() end
        function on_load()
            for i = 1, 10000 do btd5.log("info", "nested") end
        end
    )", "partial-hook-ticks.lua"));
    REQUIRE_FALSE(short_calls.invoke("on_load"));
    REQUIRE(short_calls.last_error().find("CPU instruction budget") != std::string_view::npos);
}

TEST_CASE("caught inner failures preserve outer limits across lifecycle event and timer paths", "[.boundaries]") {
    for (const std::string mode : {"lifecycle", "event", "timer"}) {
        for (const bool exhaust : {false, true}) {
            INFO(mode << ": budget exhaustion=" << exhaust);
            LuaMod* active = nullptr;
            LuaModOptions options;
            options.mod_id = "test.nested-paths";
            options.instruction_budget = 5'000;
            options.log = [&](auto, auto message) {
                if (message != "nested") return;
                if (mode == "lifecycle") (void)active->invoke("on_ready");
                else if (mode == "event") (void)active->dispatch_event("round.started");
                else active->advance_timers(1);
            };
            LuaMod mod(options);
            active = &mod;
            const std::string body = exhaust ? "while true do end" : "error('inner error')";
            const std::string tail = exhaust ? "" : "for i = 1, 100000 do end";
            REQUIRE(mod.load_script(
                "function inner() " + body + " end "
                "function on_ready() inner() end "
                "btd5.events.on('round.started', inner) "
                "btd5.timer.after(1, inner) "
                "function on_load() btd5.log('info', 'nested') " + tail + " end "
                "function on_shutdown() end", "nested-paths.lua"));
            REQUIRE_FALSE(mod.invoke("on_load"));
            REQUIRE(mod.last_error().find("CPU instruction budget") != std::string_view::npos);
            for (int attempt = 0; attempt < 3; ++attempt) REQUIRE(mod.invoke("on_shutdown"));
        }
    }
}

TEST_CASE("nested time exhaustion and recursion refusal leave the host reusable", "[.boundaries]") {
    LuaMod* active = nullptr;
    bool inner_succeeded = true;
    LuaModOptions options;
    options.mod_id = "test.nested-deadline";
    options.callback_time_limit = std::chrono::milliseconds(20);
    options.log = [&](auto, auto message) {
        if (message == "nested") inner_succeeded = active->invoke("on_ready");
        else if (message == "delay") std::this_thread::sleep_for(std::chrono::milliseconds(40));
    };
    LuaMod mod(options);
    active = &mod;
    REQUIRE(mod.load_script(R"(
        function on_load() btd5.log("info", "nested") end
        function on_ready() btd5.log("info", "delay") end
        function on_shutdown() end
    )", "deadline.lua"));
    REQUIRE_FALSE(mod.invoke("on_load"));
    REQUIRE_FALSE(inner_succeeded);
    REQUIRE(mod.last_error().find("callback time limit") != std::string_view::npos);
    REQUIRE(mod.invoke("on_shutdown"));

    options.log = [&](auto, auto message) {
        if (message == "late") {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            inner_succeeded = active->invoke("on_ready");
        }
    };
    LuaMod late(options);
    active = &late;
    REQUIRE(late.load_script(R"(
        function on_load() btd5.log("info", "late") end
        function on_ready() end
    )", "expired-parent.lua"));
    REQUIRE_FALSE(late.invoke("on_load"));
    REQUIRE_FALSE(inner_succeeded);

    options.callback_recursion_limit = 1;
    options.callback_time_limit = std::chrono::milliseconds(100);
    options.log = [&](auto, auto message) {
        if (message == "nested") inner_succeeded = active->invoke("on_ready");
    };
    LuaMod shallow(options);
    active = &shallow;
    REQUIRE(shallow.load_script(R"(
        function on_load() btd5.log("info", "nested") end
        function on_ready() end
        function on_shutdown() end
    )", "recursion.lua"));
    REQUIRE(shallow.invoke("on_load"));
    REQUIRE_FALSE(inner_succeeded);
    REQUIRE(shallow.last_error().find("callback recursion limit") != std::string_view::npos);
    REQUIRE(shallow.invoke("on_shutdown"));
    REQUIRE(mod.invoke("on_shutdown"));
}

TEST_CASE("Lua errors unwind C++ host objects", "[.boundaries]") {
    lua_State* state = luaL_newstate();
    REQUIRE(state != nullptr);
    bool destroyed = false;
    lua_pushcfunction(state, +[](lua_State* call) -> int {
        struct Guard {
            bool* destroyed;
            ~Guard() { *destroyed = true; }
        } guard{static_cast<bool*>(lua_touserdata(call, 1))};
        return luaL_error(call, "unwind host objects");
    });
    lua_pushlightuserdata(state, &destroyed);
    const int status = lua_pcall(state, 1, 0, 0);
    lua_close(state);
    REQUIRE(status == LUA_ERRRUN);
    REQUIRE(destroyed);
}

TEST_CASE("lifecycle lookup ignores global metamethods", "[.boundaries]") {
    LuaModOptions options;
    options.mod_id = "test.boundary-lookup";
    options.instruction_budget = 2'000;
    LuaMod mod(options);
    REQUIRE(mod.load_script(R"(
        setmetatable(_G, {__index = function() while true do end end})
    )", "lookup-loop.lua"));
    REQUIRE(mod.invoke("on_ready"));
    REQUIRE(mod.load_script(R"(
        setmetatable(_G, {__index = function() error("lookup escaped") end})
        function on_load() error("contained") end
        function on_ready() end
    )", "lookup-error.lua"));
    REQUIRE_FALSE(mod.invoke("on_load"));
    REQUIRE(mod.callback_disabled("on_load"));
    REQUIRE(mod.last_error().find("test.boundary-lookup.on_load") != std::string_view::npos);
    REQUIRE(mod.invoke("on_ready"));
}

TEST_CASE("low memory setup and payload failures stay inside the Lua boundary", "[.boundaries]") {
    for (std::size_t limit = 1'024; limit <= 32'768; limit += 1'024) {
        LuaModOptions options;
        options.mod_id = "test.boundary-setup";
        options.memory_limit_bytes = limit;
        LuaMod mod(options);
        if (!mod.valid()) REQUIRE_FALSE(mod.last_error().empty());
        REQUIRE(mod.memory_used() <= limit);
    }

    LuaModOptions options;
    options.mod_id = "test.boundary-payload";
    options.memory_limit_bytes = 64 * 1024;
    GameObjectRegistry registry;
    int tower = 0;
    const auto handle = registry.add(GameObjectKind::Tower, &tower);
    options.object_registry = &registry;
    LuaMod mod(options);
    REQUIRE(mod.valid());
    REQUIRE(mod.load_script(R"(
        btd5.events.on("round.started", function(event) assert(event.value == "ok") end)
        function on_ready() end
    )", "payload.lua"));
    for (int attempt = 0; attempt < 10; ++attempt) {
        REQUIRE_FALSE(mod.dispatch_event("round.started", {{"value", std::string(128 * 1024, 'x')}}).succeeded);
        REQUIRE(mod.last_error().find("test.boundary-payload.event.round.started") != std::string_view::npos);
        REQUIRE(mod.dispatch_event("round.started", {{"value", std::string("ok")}}).succeeded);
        REQUIRE(mod.invoke("on_ready"));
    }
    REQUIRE_FALSE(mod.load_script(std::string(128 * 1024, 'x'), "oversized.lua"));
    REQUIRE(mod.invoke("on_ready"));
    LuaEventFields objects;
    for (int index = 0; index < 2'000; ++index) {
        objects.emplace_back("tower" + std::to_string(index), handle);
    }
    const auto failed_objects = mod.dispatch_event("round.started", objects);
    REQUIRE_FALSE(failed_objects.succeeded);
    REQUIRE(failed_objects.handlers_invoked == 0);
    REQUIRE(mod.dispatch_event("round.started", {{"value", std::string("ok")}}).succeeded);
    LuaModOptions healthy_options;
    healthy_options.mod_id = "test.boundary-healthy";
    LuaMod healthy(healthy_options);
    REQUIRE(healthy.load_script("function on_ready() end", "healthy.lua"));
    REQUIRE(healthy.invoke("on_ready"));
}

TEST_CASE("host errors and non-string Lua errors preserve subsequent dispatch", "[.boundaries]") {
    LuaModOptions options;
    options.mod_id = "test.boundary-errors";
    options.log = [](auto, auto) { throw std::runtime_error("sink failure"); };
    LuaMod mod(options);
    REQUIRE(mod.load_script(R"(
        function on_load() btd5.log("info", "throws") end
        function on_ready() error({}) end
        function on_shutdown() end
        btd5.timer.after(1, function() error(42) end)
        btd5.events.on("round.started", function() end)
    )", "errors.lua"));
    REQUIRE_FALSE(mod.invoke("on_load"));
    REQUIRE_FALSE(mod.invoke("on_ready"));
    mod.advance_timers(1);
    REQUIRE(mod.invoke("on_shutdown"));
    REQUIRE(mod.dispatch_event("round.started").succeeded);
}

TEST_CASE("sandbox cannot schedule execution for unprotected state destruction", "[.boundaries]") {
    LuaModOptions options;
    options.mod_id = "test.boundary-destruction";
    LuaMod mod(options);
    REQUIRE(mod.load_script(R"(
        assert(coroutine == nil)
        assert(not pcall(setmetatable, {}, {__gc = function() while true do end end}))
        assert(not pcall(setmetatable, {}, {__close = function() while true do end end}))
        local mt = {}
        local object = setmetatable({}, mt)
        mt.__gc = function() while true do end end
        assert(not pcall(setmetatable, object, mt))
        saved = object -- Adding __gc later does not register a Lua finalizer.
        local protected = setmetatable({}, {__metatable = "locked"})
        assert(not pcall(setmetatable, protected, {}))
        assert(setmetatable({}, nil) ~= nil)
    )", "destruction.lua"));
}
