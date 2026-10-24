// SPDX-License-Identifier: GPL-3.0-only
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include "../../src/native/runtime/lua_mod.hpp"
#include "../../src/native/runtime/mod_manifest.hpp"

using namespace btd5loader::runtime;

namespace {

std::filesystem::path sample_directory(const std::string& name) {
    return std::filesystem::path(BTD5ML_SOURCE_DIR) / "samples" / name;
}

std::string sample_script(const std::string& name) {
    std::ifstream stream(sample_directory(name) / "lua" / "main.lua", std::ios::binary);
    REQUIRE(stream);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

bool logged(const std::vector<std::string>& logs, const std::string& fragment) {
    return std::any_of(logs.begin(), logs.end(), [&fragment](const auto& line) {
        return line.find(fragment) != std::string::npos;
    });
}

}  // namespace

TEST_CASE("manifest schema accepts documented dependency forms and rejects prefixed wildcards", "[samples][packages]") {
    std::ifstream stream(std::filesystem::path(BTD5ML_SOURCE_DIR) / "schemas" / "mod-manifest.schema.json");
    REQUIRE(stream);
    const auto schema = nlohmann::json::parse(stream);
    const std::regex pattern(schema["properties"]["dependencies"]["items"]["properties"]["version"]["pattern"]
        .get<std::string>());
    const auto version = *parse_semantic_version("1.2.3");
    for (const std::string requirement : {"*", "1.2.3", ">=1.0.0", "^1.0.0", "^0.2.3", "^0.0.3"}) {
        INFO(requirement);
        REQUIRE(std::regex_match(requirement, pattern));
    }
    REQUIRE(version_satisfies(version, "*"));
    for (const std::string requirement : {"*1.2.3", "~1.2.3", ">=1.0.0 <2.0.0", "1.0.0 || 2.0.0"}) {
        INFO(requirement);
        REQUIRE_FALSE(std::regex_match(requirement, pattern));
        REQUIRE_FALSE(version_satisfies(version, requirement));
    }
}

TEST_CASE("round budget observes synchronous cash changes and releases stale matches", "[samples][lua]") {
    GameObjectRegistry registry;
    int native_match{};
    int native_round{};
    auto match = registry.add(GameObjectKind::Match, &native_match);
    auto round = registry.add(GameObjectKind::Round, &native_round);
    std::vector<std::string> logs;
    std::vector<double> writes;
    LuaMod* running{};
    LuaModOptions options;
    options.mod_id = "example.round-budget";
    options.object_registry = &registry;
    options.configuration = {{"enabled", "true"}, {"round_cash", "1234"}};
    options.log = [&](std::string_view level, std::string_view message) {
        INFO(message);
        REQUIRE(level != "error");
        logs.emplace_back(message);
    };
    options.game_object_integer_get = [](GameObjectKind, void*, std::string_view) {
        return std::optional<std::int64_t>{1};
    };
    options.game_object_number_set = [&](GameObjectKind kind, void*, std::string_view property,
                                        double value, std::string&) {
        REQUIRE(kind == GameObjectKind::Match);
        REQUIRE(property == "cash");
        writes.push_back(value);
        REQUIRE(running->dispatch_event("cash.changed", {{"cash", value}}).succeeded);
        return true;
    };
    LuaMod mod(options);
    running = &mod;
    REQUIRE(mod.load_script(sample_script("round-budget-mod"), "lua/main.lua"));
    REQUIRE(mod.invoke("on_load"));
    REQUIRE(mod.dispatch_event("match.started", {{"match", match}}).succeeded);
    REQUIRE(mod.dispatch_event("round.started", {{"round", round}}).succeeded);
    REQUIRE(writes == std::vector<double>{1234.0});
    REQUIRE(logged(logs, "observed=1234"));
    registry.begin_scene();
    REQUIRE(mod.dispatch_event("match.ended", {{"match", match}}).succeeded);
    // No stale payload is admitted by the host; probe the sample's no-match guard.
    REQUIRE(mod.dispatch_event("round.started").succeeded);
    REQUIRE(writes.size() == 1);
    match = registry.add(GameObjectKind::Match, &native_match);
    round = registry.add(GameObjectKind::Round, &native_round);
    REQUIRE(mod.dispatch_event("match.started", {{"match", match}}).succeeded);
    REQUIRE(mod.dispatch_event("round.started", {{"round", round}}).succeeded);
    REQUIRE(writes.size() == 2);

    options.configuration.clear();
    LuaMod disabled(options);
    REQUIRE(disabled.load_script(sample_script("round-budget-mod"), "lua/main.lua"));
    REQUIRE(disabled.invoke("on_load"));
    REQUIRE(disabled.dispatch_event("match.started", {{"match", match}}).succeeded);
    REQUIRE(disabled.dispatch_event("round.started", {{"round", round}}).succeeded);
    REQUIRE(writes.size() == 2);
}

TEST_CASE("tower rules bound rejections reset each match and check sold wrapper safely", "[samples][lua]") {
    GameObjectRegistry registry;
    int native_tower{};
    auto tower = registry.add(GameObjectKind::Tower, &native_tower);
    std::int64_t pops{};
    std::int64_t sale{};
    std::vector<std::string> logs;
    LuaModOptions options;
    options.mod_id = "example.tower-rules";
    options.object_registry = &registry;
    options.configuration = {{"block_first_purchase", "true"}, {"block_first_upgrade", "true"},
        {"block_first_sale", "true"}, {"override_properties", "true"},
        {"pop_count", "123"}, {"sell_price", "777"}};
    options.log = [&](std::string_view level, std::string_view message) {
        REQUIRE(level != "error");
        logs.emplace_back(message);
    };
    options.game_object_text_get = [](GameObjectKind, void*, std::string_view) {
        return std::optional<std::string>{"DartMonkey"};
    };
    options.game_object_integer_get = [&](GameObjectKind, void*, std::string_view property) {
        return std::optional<std::int64_t>{property == "pop_count" ? pops : property == "sell_price" ? sale : 0};
    };
    options.game_object_integer_set = [&](GameObjectKind, void*, std::string_view property,
                                         std::int64_t value, std::string&) {
        if (property == "pop_count") pops = value;
        else if (property == "sell_price") sale = value;
        else return false;
        return true;
    };
    LuaMod mod(options);
    REQUIRE(mod.load_script(sample_script("tower-rules-mod"), "lua/main.lua"));
    REQUIRE_FALSE(mod.dispatch_event("tower.placing", {{"tower", tower}, {"purchase", false}}, true).cancelled);
    for (int match_number = 0; match_number < 2; ++match_number) {
        REQUIRE(mod.dispatch_event("match.started").succeeded);
        for (const auto* name : {"tower.placing", "tower.upgrading", "tower.selling"}) {
            const LuaEventFields fields{{"tower", tower}, {"purchase", true}};
            const auto first = mod.dispatch_event(name, fields, true);
            REQUIRE(first.succeeded);
            REQUIRE(first.cancelled);
            const auto second = mod.dispatch_event(name, fields, true);
            REQUIRE(second.succeeded);
            REQUIRE_FALSE(second.cancelled);
        }
    }
    REQUIRE(mod.dispatch_event("tower.placed", {{"tower", tower}}).succeeded);
    REQUIRE(pops == 123);
    REQUIRE(sale == 777);
    REQUIRE(mod.dispatch_event("tower.sold", {{"tower", tower}}).succeeded);
    REQUIRE(registry.invalidate(tower));
    mod.advance_timers(1);
    REQUIRE(logged(logs, "stale=true"));

    options.configuration.clear();
    pops = 0;
    sale = 0;
    LuaMod disabled(options);
    REQUIRE(disabled.load_script(sample_script("tower-rules-mod"), "lua/main.lua"));
    tower = registry.add(GameObjectKind::Tower, &native_tower);
    REQUIRE(disabled.dispatch_event("tower.placed", {{"tower", tower}}).succeeded);
    REQUIRE(pops == 0);
    REQUIRE(sale == 0);
    REQUIRE_FALSE(disabled.dispatch_event("tower.selling", {{"tower", tower}}, true).cancelled);
}

TEST_CASE("bloon rules bound operations including repeated leaks and ignore non-round spawns", "[samples][lua]") {
    GameObjectRegistry registry;
    int native_bloon{};
    const auto bloon = registry.add(GameObjectKind::Bloon, &native_bloon);
    double health = 1;
    LuaModOptions options;
    options.mod_id = "example.bloon-rules";
    options.object_registry = &registry;
    options.configuration = {{"cancel_first_spawn", "true"}, {"cancel_first_pop", "true"},
        {"cancel_first_leak", "true"}, {"boost_first_health", "true"}, {"health_bonus", "2"}};
    options.log = [](std::string_view level, std::string_view) { REQUIRE(level != "error"); };
    options.game_object_text_get = [](GameObjectKind, void*, std::string_view) {
        return std::optional<std::string>{"Red"};
    };
    options.game_object_number_get = [&](GameObjectKind, void*, std::string_view) {
        return std::optional<double>{health};
    };
    options.game_object_number_set = [&](GameObjectKind, void*, std::string_view, double value, std::string&) {
        health = value;
        return true;
    };
    LuaMod mod(options);
    REQUIRE(mod.load_script(sample_script("bloon-rules-mod"), "lua/main.lua"));
    REQUIRE_FALSE(mod.dispatch_event("bloon.spawning", {{"bloon", bloon}, {"from_round", false}}, true).cancelled);
    for (int match_number = 0; match_number < 2; ++match_number) {
        REQUIRE(mod.dispatch_event("match.started").succeeded);
        for (const auto* name : {"bloon.spawning", "bloon.popping", "bloon.leaking"}) {
            const LuaEventFields fields{{"bloon", bloon}, {"from_round", true}};
            const auto first = mod.dispatch_event(name, fields, true);
            REQUIRE(first.succeeded);
            REQUIRE(first.cancelled);
            const auto second = mod.dispatch_event(name, fields, true);
            REQUIRE(second.succeeded);
            REQUIRE_FALSE(second.cancelled);
        }
        health = 1;
        REQUIRE(mod.dispatch_event("bloon.spawned", {{"bloon", bloon}}).succeeded);
        REQUIRE(health == 3);
        REQUIRE(mod.dispatch_event("bloon.spawned", {{"bloon", bloon}}).succeeded);
        REQUIRE(health == 3);
    }

    options.configuration.clear();
    LuaMod disabled(options);
    REQUIRE(disabled.load_script(sample_script("bloon-rules-mod"), "lua/main.lua"));
    REQUIRE_FALSE(disabled.dispatch_event("bloon.leaking", {{"bloon", bloon}}, true).cancelled);
    REQUIRE(disabled.dispatch_event("bloon.spawned", {{"bloon", bloon}}).succeeded);
    REQUIRE(health == 3);
}

TEST_CASE("resource notes persists launches and honors its frame timer and note setting", "[samples][lua]") {
    const auto storage = std::filesystem::path(BTD5ML_SOURCE_DIR) / "out" / "test-resource-notes-storage";
    std::filesystem::remove_all(storage);
    std::vector<std::string> logs;
    LuaModOptions options;
    options.mod_id = "example.resource-notes";
    options.resource_directory = sample_directory("resource-notes-mod");
    options.storage_directory = storage;
    options.configuration = {{"show_note", "true"}, {"delay_frames", "2"}};
    options.localization = {{"notes.ready", "Translated ready"}, {"notes.launches", "Launch count"}};
    options.log = [&](std::string_view level, std::string_view message) {
        REQUIRE(level != "error");
        logs.emplace_back(message);
    };
    {
        LuaMod mod(options);
        REQUIRE(mod.load_script(sample_script("resource-notes-mod"), "lua/main.lua"));
        REQUIRE(mod.invoke("on_load"));
        REQUIRE(mod.invoke("on_ready"));
        REQUIRE(logged(logs, "Launch count=1"));
        REQUIRE(logged(logs, "Translated ready"));
        mod.advance_timers(1);
        REQUIRE_FALSE(logged(logs, "This text came"));
        mod.advance_timers(1);
        REQUIRE(logged(logs, "This text came"));
        REQUIRE(mod.invoke("on_shutdown"));
    }
    logs.clear();
    options.configuration["show_note"] = "false";
    {
        LuaMod mod(options);
        REQUIRE(mod.load_script(sample_script("resource-notes-mod"), "lua/main.lua"));
        REQUIRE(mod.invoke("on_load"));
        REQUIRE(mod.invoke("on_ready"));
        mod.advance_timers(100);
        REQUIRE(logged(logs, "Launch count=2"));
        REQUIRE_FALSE(logged(logs, "This text came"));
    }
    std::filesystem::remove_all(storage);
}
