// SPDX-License-Identifier: GPL-3.0-only
#include <catch2/catch_test_macros.hpp>
#include "../../src/native/runtime/mod_dispatch.hpp"
#include "../../src/native/runtime/mod_lifecycle.hpp"

using namespace btd5loader::runtime;

TEST_CASE("production profile coordination retains mutations and sticky cancellation", "[lua][coordination]") {
    std::vector<std::unique_ptr<LuaMod>> mods;
    std::vector<std::string> calls;
    const auto add = [&](const std::string& script) {
        LuaModOptions options;
        options.mod_id = "coordination" + std::to_string(mods.size());
        options.log = [&](auto, auto message) { calls.emplace_back(message); };
        auto mod = std::make_unique<LuaMod>(options);
        REQUIRE(mod->load_script(script, "coordination.lua"));
        mods.push_back(std::move(mod));
    };
    add(R"(btd5.events.on('lives.changing', function(e)
        assert(e.old_lives == 100 and e.new_lives == 90)
        e.new_lives = 80; e.cancelled = true
        btd5.log('info', 'first')
    end))");
    add(R"(btd5.events.on('lives.changing', function(e)
        assert(e.new_lives == 80); e.new_lives = -1
        e.cancelled = false; btd5.log('info', 'invalid')
    end))");
    add(R"(btd5.events.on('lives.changing', function(e)
        assert(e.new_lives == 80); e.new_lives = 70
        btd5.log('info', 'last')
    end))");
    int rejected = 0;
    const auto decision = dispatch_profile_lives(mods, 100, 90, [&] { ++rejected; });
    REQUIRE(decision.cancelled);
    REQUIRE(decision.new_lives == 70);
    REQUIRE(rejected == 1);
    REQUIRE(calls == std::vector<std::string>{"first", "invalid", "last"});
}

TEST_CASE("production object cancellation visits later mods and shutdown drains them", "[lua][coordination]") {
    for (const std::string name : {"tower.upgrading", "tower.selling", "bloon.leaking"}) {
        ModDispatchGate gate;
        std::vector<std::unique_ptr<LuaMod>> mods;
        std::vector<std::string> calls;
        for (const std::string id : {"first", "last"}) {
            LuaModOptions options;
            options.mod_id = id;
            options.log = [&](auto, auto message) { calls.emplace_back(message); };
            auto mod = std::make_unique<LuaMod>(options);
            REQUIRE(mod->load_script("btd5.events.on('" + name + "', function(e) "
                "btd5.log('info', '" + id + "'); e.cancelled = " +
                (id == "first" ? "true" : "false") + " end) "
                "function on_shutdown() btd5.log('info', 'stop-" + id + "') end", "profile.lua"));
            mods.push_back(std::move(mod));
        }
        {
            auto lease = gate.enter();
            REQUIRE(static_cast<bool>(lease));
            REQUIRE(dispatch_profile_event(mods, name, {}, true));
        }
        REQUIRE(gate.request_stop());
        {
            auto lock = gate.exclusive();
            shutdown_mods(mods);
        }
        REQUIRE_FALSE(static_cast<bool>(gate.enter()));
        REQUIRE(calls == std::vector<std::string>{"first", "last", "stop-last", "stop-first"});
    }
}
