// SPDX-License-Identifier: GPL-3.0-only
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <future>
#include <string>
#include <vector>

#include "../../src/native/runtime/mod_lifecycle.hpp"
#include "../../src/native/runtime/object_types.hpp"

using namespace btd5loader::runtime;

TEST_CASE("type flags map single bits to their registration names", "[objects][identity]") {
    REQUIRE(type_flag_name(kTowerTypeNames, 0x2) == "DartMonkey");
    REQUIRE(type_flag_name(kTowerTypeNames, 0x1) == "TestTower");
    REQUIRE(type_flag_name(kTowerTypeNames, 0x800000) == "MonkeySub");
    REQUIRE(type_flag_name(kTowerTypeNames, std::uint64_t{1} << 59) == "GameDummy");
    REQUIRE(type_flag_name(kBloonTypeNames, 0x2) == "Red");
    REQUIRE(type_flag_name(kBloonTypeNames, 0x1000) == "Golden");
    REQUIRE(type_flag_name(kBloonTypeNames, 0x2000) == "MOAB");
    REQUIRE(type_flag_name(kBloonTypeNames, 0x80000) == "Dreadbloon");
    // Unknown flags never produce a guessed name.
    REQUIRE(type_flag_name(kTowerTypeNames, 0).empty());
    REQUIRE(type_flag_name(kTowerTypeNames, 0x6).empty());
    REQUIRE(type_flag_name(kTowerTypeNames, std::uint64_t{1} << 60).empty());
    REQUIRE(type_flag_name(kBloonTypeNames, 0x100000).empty());
}

namespace {

struct IdentityHost final {
    GameObjectRegistry registry;
    std::string tower_type = "DartMonkey";
    std::int64_t left = 2;
    std::int64_t right = 0;
    std::int64_t round_number = 7;
    double cash = 650.0;
    std::vector<double> cash_writes;
    LuaMod* mod{};
    std::vector<std::string> logs;

    [[nodiscard]] LuaModOptions options() {
        LuaModOptions result;
        result.mod_id = "sample.identity";
        result.object_registry = &registry;
        result.log = [this](const std::string_view level, const std::string_view message) {
            logs.emplace_back(level == "error" ? "error:" + std::string(message)
                                               : std::string(message));
        };
        result.game_object_text_get = [this](GameObjectKind kind, void*, std::string_view property)
            -> std::optional<std::string> {
            if (property != "type") return std::nullopt;
            if (kind == GameObjectKind::Tower) return tower_type;
            return std::string{};  // A bloon whose flag has no known name.
        };
        result.game_object_integer_get = [this](GameObjectKind kind, void*, std::string_view property)
            -> std::optional<std::int64_t> {
            // Like the runtime, an out-of-range native tier is unreadable.
            if (kind == GameObjectKind::Tower && property == "upgrade_tier_left") {
                return left < 0 || left > 4 ? std::nullopt : std::optional{left};
            }
            if (kind == GameObjectKind::Tower && property == "upgrade_tier_right") return right;
            if (kind == GameObjectKind::Round && property == "number") return round_number;
            return std::nullopt;
        };
        result.game_object_number_set = [this](GameObjectKind kind, void*, std::string_view property,
                                                const double value, std::string& error) {
            if (kind != GameObjectKind::Match || property != "cash") {
                error = "unexpected property";
                return false;
            }
            cash_writes.push_back(value);
            cash = value;
            // Like the native helper, a change raises the money-updated event,
            // dispatched while the setter is still on the stack.
            const auto nested = mod->dispatch_event("cash.changed", {{"cash", cash}});
            if (!nested.succeeded) logs.emplace_back("nested-failed");
            return true;
        };
        return result;
    }
};

}  // namespace

TEST_CASE("Lua identity getters read type names, tiers, and round numbers", "[lua][objects][identity]") {
    IdentityHost host;
    int tower{};
    int bloon{};
    int round{};
    const auto tower_handle = host.registry.add(GameObjectKind::Tower, &tower);
    const auto bloon_handle = host.registry.add(GameObjectKind::Bloon, &bloon);
    const auto round_handle = host.registry.add(GameObjectKind::Round, &round);
    LuaMod mod(host.options());
    host.mod = &mod;
    REQUIRE(mod.load_script(R"lua(
        btd5.events.on("tower.placed", function(e)
            assert(e.tower:type() == "DartMonkey")
            local left, right = e.tower:upgrade_tiers()
            assert(left == 2 and right == 0)
            assert(not pcall(e.tower.number, e.tower))
            assert(not pcall(e.tower.set_cash, e.tower, 10))
            btd5.log("info", "tower-ok")
        end)
        btd5.events.on("bloon.spawned", function(e)
            assert(e.bloon:type() == nil)
            assert(not pcall(e.bloon.upgrade_tiers, e.bloon))
            btd5.log("info", "bloon-ok")
        end)
        btd5.events.on("round.started", function(e)
            assert(e.round:number() == 7)
            assert(not pcall(e.round.type, e.round))
            btd5.log("info", "round-ok")
        end)
    )lua", "identity.lua"));
    REQUIRE(mod.dispatch_event("tower.placed", {{"tower", tower_handle}}).succeeded);
    REQUIRE(mod.dispatch_event("bloon.spawned", {{"bloon", bloon_handle}}).succeeded);
    REQUIRE(mod.dispatch_event("round.started", {{"round", round_handle}}).succeeded);
    REQUIRE(host.logs == std::vector<std::string>{"tower-ok", "bloon-ok", "round-ok"});
}

TEST_CASE("Lua identity getters reject stale objects and unreadable values", "[lua][objects][identity]") {
    IdentityHost host;
    int tower{};
    const auto tower_handle = host.registry.add(GameObjectKind::Tower, &tower);
    LuaMod mod(host.options());
    host.mod = &mod;
    REQUIRE(mod.load_script(R"lua(
        local saved
        btd5.events.on("tower.placed", function(e) saved = e.tower end)
        btd5.events.on("tower.upgraded", function(e)
            local ok, message = pcall(e.tower.upgrade_tiers, e.tower)
            assert(not ok and message:find("could not be read"))
            btd5.log("info", "unreadable")
        end)
        btd5.events.on("tower.sold", function()
            local ok, message = pcall(saved.type, saved)
            assert(not ok and message:find("stale"))
            btd5.log("info", "stale")
        end)
    )lua", "identity-stale.lua"));
    REQUIRE(mod.dispatch_event("tower.placed", {{"tower", tower_handle}}).succeeded);
    host.left = -1;
    const auto unreadable = mod.dispatch_event("tower.upgraded", {{"tower", tower_handle}});
    INFO(mod.last_error());
    REQUIRE(unreadable.succeeded);
    REQUIRE(host.registry.invalidate(tower_handle));
    REQUIRE(mod.dispatch_event("tower.sold").succeeded);
    REQUIRE(host.logs == std::vector<std::string>{"unreadable", "stale"});
}

TEST_CASE("match set_cash validates values and may run inside a cash handler", "[lua][objects][identity]") {
    IdentityHost host;
    int match{};
    const auto match_handle = host.registry.add(GameObjectKind::Match, &match);
    LuaMod mod(host.options());
    host.mod = &mod;
    REQUIRE(mod.load_script(R"lua(
        local match
        btd5.events.on("match.started", function(e)
            match = e.match
            for _, bad in ipairs({ "100", -1, 0/0, math.huge, 2147483648, true }) do
                assert(not pcall(match.set_cash, match, bad))
            end
            assert(match:set_cash(1000))
            assert(match:set_cash(250.5))
        end)
        btd5.events.on("cash.changed", function(e)
            btd5.log("info", "changed:" .. e.cash)
            -- A handler may set cash again; the nested update is bounded.
            if e.cash == 250.5 then assert(match:set_cash(300)) end
        end)
        btd5.events.on("match.ended", function(e)
            local ok, message = pcall(e.match.set_cash, e.match, 10)
            assert(not ok and message:find("stale"))
            btd5.log("info", "ended")
        end)
    )lua", "identity-cash.lua"));
    REQUIRE(mod.dispatch_event("match.started", {{"match", match_handle}}).succeeded);
    REQUIRE(host.cash_writes == std::vector<double>{1000.0, 250.5, 300.0});
    REQUIRE(host.cash == 300.0);
    host.registry.begin_scene();
    REQUIRE(mod.dispatch_event("match.ended", {{"match", match_handle}}).succeeded);
    REQUIRE(host.logs == std::vector<std::string>{
        "changed:1000.0", "changed:250.5", "changed:300.0", "ended"});
}

TEST_CASE("the dispatch gate re-enters on its owning thread only", "[shutdown][identity]") {
    ModDispatchGate gate;
    std::future<bool> other;
    {
        auto outer = gate.enter();
        REQUIRE(outer);
        {
            // A native event raised by a Lua setter re-enters on the same thread.
            auto nested = gate.enter();
            REQUIRE(nested);
        }
        other = std::async(std::launch::async, [&gate] { return static_cast<bool>(gate.enter()); });
        REQUIRE(other.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
        // A nested lease still refuses work once shutdown has been requested.
        REQUIRE(gate.request_stop());
        REQUIRE_FALSE(gate.enter());
    }
    REQUIRE_FALSE(other.get());
    // The owner is cleared once the outer lease ends.
    REQUIRE_FALSE(gate.enter());
    auto lock = gate.exclusive();
    REQUIRE(lock.owns_lock());
}
