#include <btd5loader/version.hpp>
#include <btd5loader/runtime_api.hpp>
#include <catch2/catch_test_macros.hpp>
#include <miniz.h>
#include <nlohmann/json.hpp>

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../../src/native/runtime/compatibility.hpp"
#include "../../src/native/runtime/active_profile.hpp"
#include "../../src/native/runtime/bloon_action_hook.hpp"
#include "../../src/native/runtime/cash_capture.hpp"
#include "../../src/native/runtime/hook_transaction.hpp"
#include "../../src/native/runtime/lives_hook.hpp"
#include "../../src/native/runtime/lives_write_hook.hpp"
#include "../../src/native/runtime/logger.hpp"
#include "../../src/native/runtime/lua_mod.hpp"
#include "../../src/native/runtime/match_hook.hpp"
#include "../../src/native/runtime/round_hook.hpp"
#include "../../src/native/runtime/mod_manifest.hpp"
#include "../../src/native/runtime/mod_package.hpp"
#include "../../src/native/runtime/pattern.hpp"
#include "../../src/native/runtime/native_event_hook.hpp"
#include "../../src/native/runtime/object_lifetime_hook.hpp"
#include "../../src/native/runtime/teardown_ledger.hpp"
#include "../../src/native/runtime/runtime_state.hpp"
#include "../../src/native/runtime/symbol_resolver.hpp"
#include "../../src/native/runtime/tower_placement_hook.hpp"
#include "../../src/native/runtime/tower_purchase_hook.hpp"
#include "../../src/native/runtime/tower_sale_hook.hpp"
#include "../../src/native/runtime/tower_upgrade_hook.hpp"

namespace {

bool create_test_zip(
    const std::filesystem::path& path,
    const std::vector<std::pair<std::string, std::string>>& files) {
    mz_zip_archive archive{};
    const std::string native_path = path.string();
    if (!mz_zip_writer_init_file(&archive, native_path.c_str(), 0)) {
        return false;
    }
    bool succeeded = true;
    for (const auto& [name, contents] : files) {
        if (!mz_zip_writer_add_mem(
                &archive,
                name.c_str(),
                contents.data(),
                contents.size(),
                static_cast<mz_uint>(MZ_DEFAULT_COMPRESSION))) {
            succeeded = false;
            break;
        }
    }
    if (succeeded) {
        succeeded = mz_zip_writer_finalize_archive(&archive) == MZ_TRUE;
    }
    succeeded = mz_zip_writer_end(&archive) == MZ_TRUE && succeeded;
    return succeeded;
}

std::string read_test_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

struct FakeGameScreen final {
    std::vector<std::string>* calls{};
};

__declspec(noinline) void __fastcall fake_game_screen_init(
    void* const instance,
    void*,
    void* const screen_data) {
    auto* const screen = static_cast<FakeGameScreen*>(instance);
    if (screen != nullptr && screen->calls != nullptr && screen_data != nullptr) {
        screen->calls->emplace_back("original");
    }
}

__declspec(noinline) void __fastcall fake_game_screen_uninit(void* const instance, void*) {
    auto* const screen = static_cast<FakeGameScreen*>(instance);
    if (screen != nullptr && screen->calls != nullptr) {
        screen->calls->emplace_back("original_uninit");
    }
}

struct FakeRoundState final {
    int started{};
};

__declspec(noinline) void __fastcall fake_round_start(
    void* const instance,
    void*,
    void* const number) {
    auto* const round = static_cast<FakeRoundState*>(instance);
    if (round != nullptr && number != nullptr) ++round->started;
}

struct FakeTowerManager final {
    void* placed{};
    std::vector<std::string>* calls{};
};

__declspec(noinline) void __fastcall fake_tower_placement(
    void* const instance,
    void*,
    void* const tower) {
    auto* const manager = static_cast<FakeTowerManager*>(instance);
    manager->placed = tower;
    manager->calls->emplace_back("original");
}

struct FakeLifetimeObject final {
    std::vector<std::string>* calls{};
    bool constructed{};
};

using LifetimeWord = std::uintptr_t;

__declspec(noinline) void* __fastcall fake_attack_constructor(
    void* const instance, void*, const LifetimeWord first, const LifetimeWord second) {
    auto* const object = static_cast<FakeLifetimeObject*>(instance);
    object->constructed = first == 1 && second == 2;
    object->calls->emplace_back("attack_constructor");
    return instance;
}

__declspec(noinline) void __fastcall fake_attack_destructor(void* const instance, void*) {
    auto* const object = static_cast<FakeLifetimeObject*>(instance);
    object->constructed = false;
    object->calls->emplace_back("attack_destructor");
}

__declspec(noinline) void* __fastcall fake_projectile_constructor_primary(
    void* const instance, void*, const LifetimeWord a, const LifetimeWord b,
    const LifetimeWord c, const LifetimeWord d) {
    auto* const object = static_cast<FakeLifetimeObject*>(instance);
    object->constructed = a + b + c + d == 10;
    object->calls->emplace_back("projectile_primary");
    return instance;
}

__declspec(noinline) void* __fastcall fake_projectile_constructor_secondary(
    void* const instance, void*, const LifetimeWord a, const LifetimeWord b,
    const LifetimeWord c, const LifetimeWord d) {
    auto* const object = static_cast<FakeLifetimeObject*>(instance);
    object->constructed = a * b * c * d == 24;
    object->calls->emplace_back("projectile_secondary");
    return instance;
}

__declspec(noinline) void* __fastcall fake_projectile_constructor_tertiary(
    void* const instance, void*, const LifetimeWord a, const LifetimeWord b,
    const LifetimeWord c, const LifetimeWord d, const LifetimeWord e, const LifetimeWord f) {
    auto* const object = static_cast<FakeLifetimeObject*>(instance);
    object->constructed = a + b + c + d + e + f == 21;
    object->calls->emplace_back("projectile_tertiary");
    return instance;
}

__declspec(noinline) void __fastcall fake_projectile_destructor(void* const instance, void*) {
    auto* const object = static_cast<FakeLifetimeObject*>(instance);
    object->constructed = false;
    object->calls->emplace_back("projectile_destructor");
}

__declspec(noinline) void __fastcall fake_tower_destructor(void* const instance, void*) {
    auto* const object = static_cast<FakeLifetimeObject*>(instance);
    object->constructed = false;
    object->calls->emplace_back("tower_destructor");
}

__declspec(noinline) void __fastcall fake_bloon_destructor(void* const instance, void*) {
    auto* const object = static_cast<FakeLifetimeObject*>(instance);
    object->constructed = false;
    object->calls->emplace_back("bloon_destructor");
}

struct FakePurchaseScreen final {
    bool free_token{};
    bool affordable{true};
    int free_checks{};
    int cost_checks{};
    int cash{100};
};

__declspec(noinline) std::uint32_t __fastcall fake_free_token_check(
    void* const instance, void*, void* const tower) {
    auto* const screen = static_cast<FakePurchaseScreen*>(instance);
    ++screen->free_checks;
    if (tower == nullptr || !screen->free_token) return 0;
    screen->free_token = false;
    return 1;
}

__declspec(noinline) std::uint32_t __fastcall fake_cost_check(
    void* const instance, void*, void* const tower) {
    auto* const screen = static_cast<FakePurchaseScreen*>(instance);
    ++screen->cost_checks;
    if (tower == nullptr || !screen->affordable) return 0;
    screen->cash -= 40;
    return 1;
}

struct FakeEventManager final {
    std::vector<std::string>* calls{};
};

struct FakeNativeEvent final {
    void* vtable{};
};

struct FakeLivesState final {
    std::array<std::byte, 0x88> padding{};
    std::int32_t lives{};
};

struct FakeGainLivesObserver final {
    std::array<std::byte, 0x40C> padding{};
    FakeLivesState* state{};
};

struct FakeLossLivesObserver final {
    std::array<std::byte, 0x42C> padding{};
    FakeLivesState* state{};
};

__declspec(noinline) void __fastcall fake_gain_lives_handler(
    void* const instance,
    void*,
    void* const event,
    bool) {
    auto* const observer = static_cast<FakeGainLivesObserver*>(instance);
    if (observer != nullptr && observer->state != nullptr && event != nullptr) {
        observer->state->lives += *static_cast<const std::int32_t*>(event);
    }
}

__declspec(noinline) void __fastcall fake_loss_lives_handler(
    void* const instance,
    void*,
    void* const event,
    bool) {
    auto* const observer = static_cast<FakeLossLivesObserver*>(instance);
    if (observer != nullptr && observer->state != nullptr && event != nullptr) {
        observer->state->lives -= *static_cast<const std::int32_t*>(event);
    }
}

extern "C" __declspec(naked) void fake_gain_lives_write() {
    __asm {
        _emit 0x01
        _emit 0x88
        _emit 0x88
        _emit 0x00
        _emit 0x00
        _emit 0x00
        ret
    }
}

extern "C" __declspec(naked) void fake_loss_lives_write() {
    __asm {
        _emit 0x29
        _emit 0x88
        _emit 0x88
        _emit 0x00
        _emit 0x00
        _emit 0x00
        ret
    }
}

void* resolve_fake_lives_write_target(void* target) {
    auto* entry = static_cast<std::byte*>(target);
    for (int depth = 0; depth < 4 && entry[0] == std::byte{0xE9}; ++depth) {
        std::int32_t displacement = 0;
        std::memcpy(&displacement, entry + 1, sizeof(displacement));
        entry += 5 + displacement;
    }
    return entry;
}

void invoke_fake_lives_write(
    void* const state,
    const std::int32_t amount,
    void* const target) {
    __asm {
        mov eax, state
        mov ecx, amount
        call dword ptr [target]
    }
}

__declspec(noinline) bool __fastcall fake_event_dispatch(
    void* const instance,
    void*,
    void* const event,
    const bool queued) {
    auto* const manager = static_cast<FakeEventManager*>(instance);
    auto* const native_event = static_cast<FakeNativeEvent*>(event);
    if (manager != nullptr && manager->calls != nullptr) {
        manager->calls->emplace_back(queued ? "original_queued" : "original");
    }
    if (native_event != nullptr) {
        native_event->vtable = nullptr;
    }
    return event != nullptr;
}

}  // namespace

TEST_CASE("product metadata is available", "[foundation]") {
    REQUIRE(btd5loader::kProductName == "BTD5 Mod Loader");
    REQUIRE_FALSE(btd5loader::kVersion.empty());
}

TEST_CASE("active profile handoff requires absolute unique package paths", "[profiles]") {
    const auto root = std::filesystem::temp_directory_path() /
                      (L"btd5ml-active-profile-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                       std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(root);
    const auto handoff = root / L"active-profile.json";
    const auto archive = root / L"sample.btd5mod";
    const nlohmann::json document{
        {"schemaVersion", 1},
        {"profile", "Testing"},
        {"buildId", "fixture-build"},
        {"mods",
         {{{"id", "sample.lifecycle"},
           {"version", "1.0.0"},
           {"archivePath", archive.string()},
           {"configuration", {{"greeting", "hello"}}}}}}};
    {
        std::ofstream output(handoff, std::ios::binary);
        output << document.dump();
    }
    std::string error;
    const auto profile = btd5loader::runtime::load_active_profile(handoff, error);
    REQUIRE(profile.has_value());
    REQUIRE(profile->name == "Testing");
    REQUIRE(profile->mods.size() == 1);
    REQUIRE(profile->mods.front().configuration.at("greeting") == "hello");

    auto unsafe = document;
    unsafe["mods"][0]["archivePath"] = "relative.btd5mod";
    {
        std::ofstream output(handoff, std::ios::binary | std::ios::trunc);
        output << unsafe.dump();
    }
    REQUIRE_FALSE(btd5loader::runtime::load_active_profile(handoff, error).has_value());
    std::filesystem::remove_all(root);
}

TEST_CASE("native targets are built for the 32-bit game", "[foundation]") {
    STATIC_REQUIRE(sizeof(void*) == 4);
}

TEST_CASE("runtime lifecycle accepts only forward transitions", "[runtime]") {
    using btd5loader::runtime::State;
    btd5loader::runtime::StateMachine state;

    REQUIRE(state.current() == State::NotStarted);
    REQUIRE(state.transition_to(State::Bootstrap));
    REQUIRE_FALSE(state.transition_to(State::GameReady));
    REQUIRE(state.transition_to(State::CompatibilityCheck));
    REQUIRE(state.transition_to(State::HooksReady));
    REQUIRE(state.transition_to(State::ModsLoading));
    REQUIRE(state.transition_to(State::GameReady));
    REQUIRE(state.transition_to(State::ShuttingDown));
    REQUIRE_FALSE(state.transition_to(State::Failed));
}

TEST_CASE("runtime lifecycle can fail closed", "[runtime]") {
    using btd5loader::runtime::State;
    btd5loader::runtime::StateMachine state;

    REQUIRE(state.transition_to(State::Failed));
    REQUIRE(state.current() == State::Failed);
    REQUIRE(state.transition_to(State::ShuttingDown));
}

TEST_CASE("runtime can shut down while waiting for the game-ready hook", "[runtime]") {
    using btd5loader::runtime::State;
    btd5loader::runtime::StateMachine state;

    REQUIRE(state.transition_to(State::Bootstrap));
    REQUIRE(state.transition_to(State::CompatibilityCheck));
    REQUIRE(state.transition_to(State::HooksReady));
    REQUIRE(state.transition_to(State::ModsLoading));
    REQUIRE(state.transition_to(State::ShuttingDown));
}

TEST_CASE("byte patterns support wildcards and report every match", "[symbols]") {
    const std::array<std::byte, 7> bytes{
        std::byte{0x55}, std::byte{0x8b}, std::byte{0xec}, std::byte{0x90},
        std::byte{0x55}, std::byte{0x7f}, std::byte{0xec}};
    std::string error;
    const auto pattern = btd5loader::runtime::parse_pattern("55 ?? EC", error);
    REQUIRE(pattern.has_value());
    REQUIRE(error.empty());
    REQUIRE(btd5loader::runtime::find_pattern(bytes, *pattern) ==
            std::vector<std::size_t>{0, 4});
}

TEST_CASE("invalid patterns fail with a diagnostic", "[symbols]") {
    std::string error;
    REQUIRE_FALSE(btd5loader::runtime::parse_pattern("55 XYZ", error).has_value());
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("game object handles reject invalidation, reuse, and scene changes", "[objects]") {
    using btd5loader::runtime::GameObjectKind;
    btd5loader::runtime::GameObjectRegistry registry;
    std::array<int, 7> objects{};
    const std::array kinds{
        GameObjectKind::Match,
        GameObjectKind::Round,
        GameObjectKind::Player,
        GameObjectKind::Tower,
        GameObjectKind::Attack,
        GameObjectKind::Projectile,
        GameObjectKind::Bloon};
    std::vector<btd5loader::runtime::GameObjectHandle> handles;
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        const auto handle = registry.add(kinds[index], &objects[index]);
        REQUIRE(handle.id != 0);
        REQUIRE(registry.resolve(handle, kinds[index]) == &objects[index]);
        REQUIRE(registry.resolve(handle, GameObjectKind::Match) ==
                (kinds[index] == GameObjectKind::Match ? &objects[index] : nullptr));
        handles.push_back(handle);
    }

    const auto stale = handles[3];
    REQUIRE(registry.find_or_add(GameObjectKind::Tower, &objects[3]) == stale);
    REQUIRE(registry.invalidate(stale));
    REQUIRE_FALSE(registry.invalidate(stale));
    REQUIRE(registry.resolve(stale, GameObjectKind::Tower) == nullptr);
    int replacement = 0;
    const auto reused = registry.add(GameObjectKind::Tower, &replacement);
    REQUIRE(reused.id == stale.id);
    REQUIRE(reused.generation != stale.generation);
    REQUIRE(registry.resolve(reused, GameObjectKind::Tower) == &replacement);

    registry.begin_scene();
    for (const auto& handle : handles) {
        REQUIRE(registry.resolve(handle, handle.kind) == nullptr);
    }
    REQUIRE(registry.resolve(reused, GameObjectKind::Tower) == nullptr);
    REQUIRE_FALSE(registry.find(GameObjectKind::Tower, &replacement).has_value());
    REQUIRE(registry.live_count() == 0);
}

TEST_CASE("game object lookup is indexed by kind and follows invalidation", "[objects]") {
    using btd5loader::runtime::GameObjectKind;
    btd5loader::runtime::GameObjectRegistry registry;
    int shared = 0;
    REQUIRE_FALSE(registry.find(GameObjectKind::Projectile, &shared).has_value());
    REQUIRE_FALSE(registry.find(GameObjectKind::Projectile, nullptr).has_value());
    const auto attack = registry.add(GameObjectKind::Attack, &shared);
    const auto projectile = registry.add(GameObjectKind::Projectile, &shared);
    REQUIRE(attack.id != projectile.id);
    REQUIRE(registry.find(GameObjectKind::Attack, &shared) == attack);
    REQUIRE(registry.find(GameObjectKind::Projectile, &shared) == projectile);
    REQUIRE(registry.find_or_add(GameObjectKind::Projectile, &shared) == projectile);
    REQUIRE(registry.live_count() == 2);
    REQUIRE(registry.live_count(GameObjectKind::Attack) == 1);
    REQUIRE(registry.live_count(GameObjectKind::Bloon) == 0);

    REQUIRE(registry.invalidate(projectile));
    REQUIRE_FALSE(registry.find(GameObjectKind::Projectile, &shared).has_value());
    REQUIRE(registry.find(GameObjectKind::Attack, &shared) == attack);
    REQUIRE(registry.live_count() == 1);

    // A reallocated native object at the same address receives a fresh generation.
    const auto replacement = registry.find_or_add(GameObjectKind::Projectile, &shared);
    REQUIRE(replacement.id == projectile.id);
    REQUIRE(replacement.generation != projectile.generation);
    REQUIRE(registry.resolve(projectile, GameObjectKind::Projectile) == nullptr);
    REQUIRE(registry.resolve(replacement, GameObjectKind::Projectile) == &shared);

    // Churn many short-lived objects; slots are reused rather than growing.
    std::array<int, 64> burst{};
    for (int round = 0; round < 100; ++round) {
        std::vector<btd5loader::runtime::GameObjectHandle> live;
        for (auto& object : burst) {
            live.push_back(registry.add(GameObjectKind::Projectile, &object));
        }
        for (const auto& handle : live) {
            REQUIRE(registry.invalidate(handle));
        }
    }
    REQUIRE(registry.live_count() == 2);
    registry.begin_scene();
    REQUIRE(registry.live_count() == 0);
    REQUIRE_FALSE(registry.find(GameObjectKind::Attack, &shared).has_value());
}

TEST_CASE("Lua events are ordered, mutable, cancellable, and unsubscribable", "[lua][events]") {
    std::vector<std::string> logs;
    btd5loader::runtime::LuaModOptions options;
    options.mod_id = "sample.events";
    options.log = [&logs](const std::string_view, const std::string_view message) {
        logs.emplace_back(message);
    };
    btd5loader::runtime::LuaMod mod(std::move(options));
    REQUIRE(mod.load_script(R"lua(
        local late
        local first = btd5.events.on("match.starting", function(event)
            btd5.log("info", "first:" .. event.value)
            event.value = event.value + 1
            if late == nil then
                late = btd5.events.on("match.starting", function(e)
                    btd5.log("info", "late:" .. e.value)
                end)
            end
        end)
        btd5.events.on("match.starting", function(event)
            btd5.log("info", "second:" .. event.value)
            event.cancelled = true
        end)
        local removed = btd5.events.on("match.starting", function()
            error("removed handler ran")
        end)
        assert(btd5.events.off(removed))
        assert(not btd5.events.off(removed))
        assert(not pcall(btd5.events.on, "unknown.event", function() end))
    )lua", "events.lua"));

    const btd5loader::runtime::LuaEventFields fields{{"value", std::int64_t{4}}};
    const auto first = mod.dispatch_event("match.starting", fields, true);
    REQUIRE(first.succeeded);
    REQUIRE(first.cancelled);
    REQUIRE(first.handlers_invoked == 2);
    REQUIRE(std::get<std::int64_t>(first.fields.at(0).second) == 5);
    REQUIRE(logs == std::vector<std::string>{"first:4", "second:5"});

    logs.clear();
    const auto second = mod.dispatch_event("match.starting", fields, false);
    REQUIRE(second.succeeded);
    REQUIRE_FALSE(second.cancelled);
    REQUIRE(second.handlers_invoked == 3);
    REQUIRE(std::get<std::int64_t>(second.fields.at(0).second) == 5);
    REQUIRE(logs == std::vector<std::string>{"first:4", "second:5", "late:5"});
}

TEST_CASE("Lua event mutation rejects incompatible field types", "[lua][events]") {
    btd5loader::runtime::LuaModOptions options;
    options.mod_id = "sample.invalid-mutation";
    btd5loader::runtime::LuaMod mod(std::move(options));
    REQUIRE(mod.load_script(R"lua(
        btd5.events.on("lives.changing", function(event)
            event.new_lives = "invalid"
        end)
    )lua", "invalid-mutation.lua"));

    const btd5loader::runtime::LuaEventFields fields{
        {"old_lives", std::int64_t{100}}, {"new_lives", std::int64_t{95}}};
    const auto result = mod.dispatch_event("lives.changing", fields, true);
    REQUIRE_FALSE(result.succeeded);
    REQUIRE(result.fields.size() == 2);
    REQUIRE(std::get<std::int64_t>(result.fields.at(1).second) == 95);
    REQUIRE(mod.last_error().find("new_lives") != std::string_view::npos);
}

TEST_CASE("event payload inspection never executes Lua metatable code", "[lua][events]") {
    btd5loader::runtime::LuaModOptions options;
    options.mod_id = "sample.payload-metatable";
    btd5loader::runtime::LuaMod mod(std::move(options));
    REQUIRE(mod.load_script(R"lua(
        local reads = 0
        btd5.events.on("lives.changing", function(event)
            event.cancelled = nil
            event.new_lives = nil
            setmetatable(event, { __index = function()
                reads = reads + 1
                error("payload inspection escaped the callback")
            end })
        end)
        btd5.events.on("round.started", function() assert(reads == 0) end)
    )lua", "payload-metatable.lua"));

    const btd5loader::runtime::LuaEventFields fields{{"new_lives", std::int64_t{95}}};
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto result = mod.dispatch_event("lives.changing", fields, true);
        REQUIRE_FALSE(result.succeeded);
        REQUIRE_FALSE(result.cancelled);
        REQUIRE(std::get<std::int64_t>(result.fields.at(0).second) == 95);
        REQUIRE(mod.dispatch_event("round.started").succeeded);
    }
}

TEST_CASE("numeric event mutations reject coercion and non-finite values", "[lua][events]") {
    btd5loader::runtime::LuaModOptions options;
    options.mod_id = "sample.numeric-mutation";
    btd5loader::runtime::LuaMod mod(std::move(options));
    REQUIRE(mod.load_script(R"lua(
        local values = { "42", math.huge, -math.huge, 0/0, false, {}, 4.5, 6 }
        local index = 0
        btd5.events.on("cash.changing", function(event)
            index = index + 1
            event.value = values[index]
        end)
    )lua", "numeric-mutation.lua"));

    const btd5loader::runtime::LuaEventFields fields{{"value", 3.0}};
    for (int attempt = 0; attempt < 6; ++attempt) {
        const auto result = mod.dispatch_event("cash.changing", fields);
        REQUIRE_FALSE(result.succeeded);
        REQUIRE(std::get<double>(result.fields.at(0).second) == 3.0);
    }
    const auto fractional = mod.dispatch_event("cash.changing", fields);
    REQUIRE(fractional.succeeded);
    REQUIRE(std::get<double>(fractional.fields.at(0).second) == 4.5);
    const auto integer = mod.dispatch_event("cash.changing", fields);
    REQUIRE(integer.succeeded);
    REQUIRE(std::get<double>(integer.fields.at(0).second) == 6.0);
}

TEST_CASE("documented v1 Lua event names are accepted", "[lua][events]") {
    btd5loader::runtime::LuaModOptions options;
    options.mod_id = "sample.event-catalog";
    btd5loader::runtime::LuaMod mod(std::move(options));
    REQUIRE(mod.load_script(R"lua(
        local names = {
            "match.starting", "match.started", "match.ending", "match.ended",
            "round.starting", "round.started", "round.ending", "round.ended",
            "cash.changing", "cash.changed", "lives.changing", "lives.changed",
            "tower.placing", "tower.placed", "tower.upgrading", "tower.upgraded",
            "tower.selling", "tower.sold", "bloon.spawning", "bloon.spawned",
            "bloon.popping", "bloon.popped", "bloon.leaking", "bloon.leaked",
            "attack.created", "attack.destroyed", "projectile.created", "projectile.destroyed"
        }
        for _, name in ipairs(names) do
            btd5.events.on(name, function() end)
        end
    )lua", "event-catalog.lua"));
    constexpr std::array names{
        "match.starting", "match.started", "match.ending", "match.ended",
        "round.starting", "round.started", "round.ending", "round.ended",
        "cash.changing", "cash.changed", "lives.changing", "lives.changed",
        "tower.placing", "tower.placed", "tower.upgrading", "tower.upgraded",
        "tower.selling", "tower.sold", "bloon.spawning", "bloon.spawned",
        "bloon.popping", "bloon.popped", "bloon.leaking", "bloon.leaked",
        "attack.created", "attack.destroyed", "projectile.created", "projectile.destroyed"};
    for (const auto* const name : names) {
        INFO("Missing event coverage for " << name);
        REQUIRE(mod.has_event_handlers(name));
        REQUIRE(mod.dispatch_event(name).handlers_invoked == 1);
    }
    REQUIRE_FALSE(mod.has_event_handlers("projectile.unknown"));
}

TEST_CASE("failing and recursive Lua event handlers are contained", "[lua][events]") {
    std::vector<std::string> logs;
    btd5loader::runtime::LuaMod* active = nullptr;
    btd5loader::runtime::LuaModOptions options;
    options.mod_id = "sample.event-errors";
    options.event_recursion_limit = 2;
    options.log = [&logs, &active](const std::string_view level, const std::string_view message) {
        logs.emplace_back(std::string(level) + ":" + std::string(message));
        if (message == "recurse" && active != nullptr) {
            (void)active->dispatch_event("round.started");
        }
    };
    btd5loader::runtime::LuaMod mod(std::move(options));
    active = &mod;
    REQUIRE(mod.load_script(R"lua(
        btd5.events.on("match.started", function() error("contained") end)
        btd5.events.on("match.started", function() btd5.log("info", "healthy") end)
        btd5.events.on("round.started", function() btd5.log("info", "recurse") end)
    )lua", "event-errors.lua"));

    const auto first = mod.dispatch_event("match.started");
    REQUIRE_FALSE(first.succeeded);
    REQUIRE(first.handlers_invoked == 2);
    REQUIRE(std::find(logs.begin(), logs.end(), "info:healthy") != logs.end());
    logs.clear();
    const auto second = mod.dispatch_event("match.started");
    REQUIRE(second.succeeded);
    REQUIRE(second.handlers_invoked == 1);

    const auto recursive = mod.dispatch_event("round.started");
    REQUIRE(recursive.succeeded);
    REQUIRE(mod.last_error().find("event recursion limit") != std::string_view::npos);
}

TEST_CASE("Lua game object userdata rejects stale host objects", "[lua][objects]") {
    using btd5loader::runtime::GameObjectKind;
    btd5loader::runtime::GameObjectRegistry registry;
    int tower = 0;
    const auto handle = registry.add(GameObjectKind::Tower, &tower);
    std::vector<std::string> logs;
    btd5loader::runtime::LuaModOptions options;
    options.mod_id = "sample.objects";
    options.object_registry = &registry;
    options.log = [&logs](const std::string_view, const std::string_view message) {
        logs.emplace_back(message);
    };
    btd5loader::runtime::LuaMod mod(std::move(options));
    REQUIRE(mod.load_script(R"lua(
        local saved
        btd5.events.on("tower.placed", function(event)
            saved = event.tower
            assert(saved:is_valid())
            assert(saved:kind() == "tower")
            btd5.log("info", "tower:" .. saved:id())
        end)
        btd5.events.on("tower.upgraded", function()
            assert(not saved:is_valid())
            saved:id()
        end)
    )lua", "objects.lua"));

    const auto placed = mod.dispatch_event(
        "tower.placed",
        {{"tower", handle}});
    REQUIRE(placed.succeeded);
    REQUIRE(logs == std::vector<std::string>{"tower:" + std::to_string(handle.id)});
    REQUIRE(registry.invalidate(handle));
    const auto stale = mod.dispatch_event("tower.upgraded");
    REQUIRE_FALSE(stale.succeeded);
    REQUIRE(mod.last_error().find("game object is stale") != std::string_view::npos);
}

TEST_CASE("Lua bloon event wrappers preserve identity and reject removal", "[lua][objects]") {
    using btd5loader::runtime::GameObjectKind;
    btd5loader::runtime::GameObjectRegistry registry;
    int bloon = 0;
    const auto spawned_handle = registry.find_or_add(GameObjectKind::Bloon, &bloon);
    const auto popped_handle = registry.find_or_add(GameObjectKind::Bloon, &bloon);
    REQUIRE(spawned_handle == popped_handle);

    std::vector<std::string> logs;
    btd5loader::runtime::LuaModOptions options;
    options.mod_id = "sample.bloon-objects";
    options.object_registry = &registry;
    options.log = [&logs](const std::string_view, const std::string_view message) {
        logs.emplace_back(message);
    };
    btd5loader::runtime::LuaMod mod(std::move(options));
    REQUIRE(mod.load_script(R"lua(
        local spawned
        btd5.events.on("bloon.spawned", function(event)
            spawned = event.bloon
            assert(spawned:is_valid())
            assert(spawned:kind() == "bloon")
        end)
        btd5.events.on("bloon.popped", function(event)
            assert(event.bloon:is_valid())
            assert(event.bloon:id() == spawned:id())
            btd5.log("info", "same-bloon")
        end)
    )lua", "bloon-objects.lua"));

    REQUIRE(mod.dispatch_event("bloon.spawned", {{"bloon", spawned_handle}}).succeeded);
    REQUIRE(mod.dispatch_event("bloon.popped", {{"bloon", popped_handle}}).succeeded);
    REQUIRE(logs == std::vector<std::string>{"same-bloon"});
    REQUIRE(registry.invalidate(popped_handle));
    REQUIRE(registry.resolve(popped_handle, GameObjectKind::Bloon) == nullptr);
    const auto stale = mod.dispatch_event("bloon.leaked", {{"bloon", popped_handle}});
    REQUIRE_FALSE(stale.succeeded);
    REQUIRE(mod.last_error().find("event payload is invalid or stale") != std::string_view::npos);
}

TEST_CASE("Lua match ended accepts only its stale terminal wrapper", "[lua][objects]") {
    using btd5loader::runtime::GameObjectKind;
    btd5loader::runtime::GameObjectRegistry registry;
    int match{};
    const auto handle = registry.add(GameObjectKind::Match, &match);
    btd5loader::runtime::LuaModOptions options;
    options.mod_id = "sample.match-terminal";
    options.object_registry = &registry;
    btd5loader::runtime::LuaMod mod(std::move(options));
    REQUIRE(mod.load_script(R"lua(
        btd5.events.on("match.ended", function(event)
            assert(not event.match:is_valid())
        end)
    )lua", "match-terminal.lua"));

    registry.begin_scene();
    REQUIRE(mod.dispatch_event("match.ended", {{"match", handle}}).succeeded);
    REQUIRE_FALSE(mod.dispatch_event("match.started", {{"match", handle}}).succeeded);
    REQUIRE(mod.last_error().find("event payload is invalid or stale") != std::string_view::npos);
}

TEST_CASE("Lua tower pop count validates writes and rejects stale objects", "[lua][objects]") {
    using btd5loader::runtime::GameObjectKind;
    btd5loader::runtime::GameObjectRegistry registry;
    std::int32_t pop_count = 7;
    const auto handle = registry.add(GameObjectKind::Tower, &pop_count);
    btd5loader::runtime::LuaModOptions options;
    options.mod_id = "sample.tower-properties";
    options.object_registry = &registry;
    options.game_object_integer_get = [](
        const GameObjectKind kind,
        void* const object,
        const std::string_view property) -> std::optional<std::int64_t> {
        if (kind != GameObjectKind::Tower || property != "pop_count" || object == nullptr) {
            return std::nullopt;
        }
        return *static_cast<std::int32_t*>(object);
    };
    options.game_object_integer_set = [](
        const GameObjectKind kind,
        void* const object,
        const std::string_view property,
        const std::int64_t value,
        std::string&) {
        if (kind != GameObjectKind::Tower || property != "pop_count" || object == nullptr) {
            return false;
        }
        *static_cast<std::int32_t*>(object) = static_cast<std::int32_t>(value);
        return true;
    };
    btd5loader::runtime::LuaMod mod(std::move(options));
    REQUIRE(mod.load_script(R"lua(
        local tower
        btd5.events.on("tower.placed", function(event)
            tower = event.tower
            assert(tower:pop_count() == 7)
            assert(tower:set_pop_count(42))
            assert(tower:pop_count() == 42)
        end)
        btd5.events.on("tower.upgraded", function()
            tower:set_pop_count(1)
        end)
    )lua", "tower-properties.lua"));

    REQUIRE(mod.dispatch_event("tower.placed", {{"tower", handle}}).succeeded);
    REQUIRE(pop_count == 42);
    REQUIRE(registry.invalidate(handle));
    const auto stale = mod.dispatch_event("tower.upgraded");
    REQUIRE_FALSE(stale.succeeded);
    REQUIRE(mod.last_error().find("game object is stale") != std::string_view::npos);
}

TEST_CASE("Lua tower pop count rejects invalid values and object kinds", "[lua][objects]") {
    using btd5loader::runtime::GameObjectKind;
    btd5loader::runtime::GameObjectRegistry registry;
    std::int32_t tower = 3;
    std::int32_t bloon = 0;
    const auto tower_handle = registry.add(GameObjectKind::Tower, &tower);
    const auto bloon_handle = registry.add(GameObjectKind::Bloon, &bloon);
    btd5loader::runtime::LuaModOptions options;
    options.mod_id = "sample.invalid-tower-properties";
    options.object_registry = &registry;
    options.game_object_integer_set = [](
        GameObjectKind, void*, std::string_view, std::int64_t, std::string&) { return true; };
    btd5loader::runtime::LuaMod mod(std::move(options));
    REQUIRE(mod.load_script(R"lua(
        btd5.events.on("tower.placed", function(event)
            event.tower:set_pop_count(-1)
        end)
        btd5.events.on("tower.upgraded", function(event)
            event.tower:set_pop_count(1.5)
        end)
        btd5.events.on("bloon.spawned", function(event)
            event.bloon:set_pop_count(1)
        end)
    )lua", "invalid-tower-properties.lua"));

    REQUIRE_FALSE(mod.dispatch_event("tower.placed", {{"tower", tower_handle}}).succeeded);
    REQUIRE(mod.last_error().find("supported range") != std::string_view::npos);
    REQUIRE_FALSE(mod.dispatch_event("tower.upgraded", {{"tower", tower_handle}}).succeeded);
    REQUIRE(mod.last_error().find("must be an integer") != std::string_view::npos);
    REQUIRE_FALSE(mod.dispatch_event("bloon.spawned", {{"bloon", bloon_handle}}).succeeded);
    REQUIRE(mod.last_error().find("only on tower") != std::string_view::npos);
    REQUIRE(tower == 3);
}

TEST_CASE("Lua tower sell price and bloon health support validated mutation", "[lua][objects]") {
    using btd5loader::runtime::GameObjectKind;
    struct TowerState final { std::int32_t sell_price{100}; } tower;
    struct BloonState final { float health{1.0F}; } bloon;
    btd5loader::runtime::GameObjectRegistry registry;
    const auto tower_handle = registry.add(GameObjectKind::Tower, &tower);
    const auto bloon_handle = registry.add(GameObjectKind::Bloon, &bloon);
    btd5loader::runtime::LuaModOptions options;
    options.mod_id = "sample.direct-properties";
    options.object_registry = &registry;
    options.game_object_integer_get = [](
        const GameObjectKind kind,
        void* const object,
        const std::string_view property) -> std::optional<std::int64_t> {
        if (kind != GameObjectKind::Tower || property != "sell_price" || object == nullptr) {
            return std::nullopt;
        }
        return static_cast<TowerState*>(object)->sell_price;
    };
    options.game_object_integer_set = [](
        const GameObjectKind kind,
        void* const object,
        const std::string_view property,
        const std::int64_t value,
        std::string&) {
        if (kind != GameObjectKind::Tower || property != "sell_price" || object == nullptr) {
            return false;
        }
        static_cast<TowerState*>(object)->sell_price = static_cast<std::int32_t>(value);
        return true;
    };
    options.game_object_number_get = [](
        const GameObjectKind kind,
        void* const object,
        const std::string_view property) -> std::optional<double> {
        if (kind != GameObjectKind::Bloon || property != "health" || object == nullptr) {
            return std::nullopt;
        }
        return static_cast<BloonState*>(object)->health;
    };
    options.game_object_number_set = [](
        const GameObjectKind kind,
        void* const object,
        const std::string_view property,
        const double value,
        std::string&) {
        if (kind != GameObjectKind::Bloon || property != "health" || object == nullptr) {
            return false;
        }
        static_cast<BloonState*>(object)->health = static_cast<float>(value);
        return true;
    };
    btd5loader::runtime::LuaMod mod(std::move(options));
    REQUIRE(mod.load_script(R"lua(
        local saved_tower
        local saved_bloon
        btd5.events.on("tower.placed", function(event)
            saved_tower = event.tower
            assert(event.tower:sell_price() == 100)
            assert(event.tower:set_sell_price(777))
            assert(event.tower:sell_price() == 777)
        end)
        btd5.events.on("bloon.spawned", function(event)
            saved_bloon = event.bloon
            assert(event.bloon:health() == 1.0)
            assert(event.bloon:set_health(2.5))
            assert(event.bloon:health() == 2.5)
        end)
        btd5.events.on("tower.upgraded", function()
            saved_tower:sell_price()
        end)
        btd5.events.on("bloon.popped", function()
            saved_bloon:health()
        end)
    )lua", "direct-properties.lua"));

    REQUIRE(mod.dispatch_event("tower.placed", {{"tower", tower_handle}}).succeeded);
    REQUIRE(mod.dispatch_event("bloon.spawned", {{"bloon", bloon_handle}}).succeeded);
    REQUIRE(tower.sell_price == 777);
    REQUIRE(bloon.health == 2.5F);
    REQUIRE(registry.invalidate(tower_handle));
    REQUIRE_FALSE(mod.dispatch_event("tower.upgraded").succeeded);
    REQUIRE(mod.last_error().find("game object is stale") != std::string_view::npos);
    REQUIRE(registry.invalidate(bloon_handle));
    REQUIRE_FALSE(mod.dispatch_event("bloon.popped").succeeded);
    REQUIRE(mod.last_error().find("game object is stale") != std::string_view::npos);
}

TEST_CASE("Lua direct properties reject invalid values", "[lua][objects]") {
    using btd5loader::runtime::GameObjectKind;
    btd5loader::runtime::GameObjectRegistry registry;
    std::int32_t tower{};
    float bloon{};
    const auto tower_handle = registry.add(GameObjectKind::Tower, &tower);
    const auto bloon_handle = registry.add(GameObjectKind::Bloon, &bloon);
    btd5loader::runtime::LuaModOptions options;
    options.mod_id = "sample.invalid-direct-properties";
    options.object_registry = &registry;
    options.game_object_integer_set = [](
        GameObjectKind, void*, std::string_view, std::int64_t, std::string&) { return true; };
    options.game_object_number_set = [](
        GameObjectKind, void*, std::string_view, double, std::string&) { return true; };
    btd5loader::runtime::LuaMod mod(std::move(options));
    REQUIRE(mod.load_script(R"lua(
        btd5.events.on("tower.placed", function(event)
            event.tower:set_sell_price(-1)
        end)
        btd5.events.on("tower.upgraded", function(event)
            event.tower:set_sell_price(1.5)
        end)
        btd5.events.on("bloon.spawned", function(event)
            event.bloon:set_health(math.huge)
        end)
        btd5.events.on("bloon.popping", function(event)
            event.bloon:set_health(-1)
        end)
        btd5.events.on("bloon.leaking", function(event)
            event.bloon:set_health("invalid")
        end)
        btd5.events.on("tower.sold", function(event)
            event.tower:set_health(1)
        end)
        btd5.events.on("bloon.popped", function(event)
            event.bloon:set_sell_price(1)
        end)
    )lua", "invalid-direct-properties.lua"));

    REQUIRE_FALSE(mod.dispatch_event("tower.placed", {{"tower", tower_handle}}).succeeded);
    REQUIRE(mod.last_error().find("supported range") != std::string_view::npos);
    REQUIRE_FALSE(mod.dispatch_event("tower.upgraded", {{"tower", tower_handle}}).succeeded);
    REQUIRE(mod.last_error().find("must be an integer") != std::string_view::npos);
    REQUIRE_FALSE(mod.dispatch_event("bloon.spawned", {{"bloon", bloon_handle}}).succeeded);
    REQUIRE(mod.last_error().find("supported range") != std::string_view::npos);
    REQUIRE_FALSE(mod.dispatch_event("bloon.popping", {{"bloon", bloon_handle}}).succeeded);
    REQUIRE(mod.last_error().find("supported range") != std::string_view::npos);
    REQUIRE_FALSE(mod.dispatch_event("bloon.leaking", {{"bloon", bloon_handle}}).succeeded);
    REQUIRE(mod.last_error().find("must be a number") != std::string_view::npos);
    REQUIRE_FALSE(mod.dispatch_event("tower.sold", {{"tower", tower_handle}}).succeeded);
    REQUIRE(mod.last_error().find("only on bloon") != std::string_view::npos);
    REQUIRE_FALSE(mod.dispatch_event("bloon.popped", {{"bloon", bloon_handle}}).succeeded);
    REQUIRE(mod.last_error().find("only on tower") != std::string_view::npos);
}

TEST_CASE("SHA-256 fingerprints are stable", "[compatibility]") {
    const auto test_root = std::filesystem::temp_directory_path() /
                           (L"btd5ml-hash-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(test_root);
    const auto test_file = test_root / L"input.bin";
    {
        std::ofstream output(test_file, std::ios::binary);
        output << "abc";
    }

    std::string error;
    const auto digest = btd5loader::runtime::sha256_file(test_file, error);
    REQUIRE(digest == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    REQUIRE(error.empty());
    std::filesystem::remove_all(test_root);
}

TEST_CASE("the initial 4.8 symbol map is valid", "[compatibility]") {
    const auto map_path = std::filesystem::path(BTD5ML_SOURCE_DIR) /
                          L"symbols" / L"btd5-steam-4.8.json";
    std::string error;
    const auto definition = btd5loader::runtime::load_build_definition(map_path, error);
    REQUIRE(definition.has_value());
    REQUIRE(error.empty());
    REQUIRE(definition->id == "steam-win32-4.8");
    REQUIRE_FALSE(definition->symbols.empty());
    REQUIRE(definition->symbols.front().name == "game.main");
    REQUIRE(definition->symbols.front().required);
}

TEST_CASE("required hook failure rolls back in reverse order", "[hooks]") {
    std::vector<std::string> events;
    btd5loader::runtime::HookTransaction transaction;
    transaction.add({
        "game.first",
        true,
        [&events] { events.emplace_back("install:first"); return true; },
        [&events] { events.emplace_back("remove:first"); }});
    transaction.add({
        "game.second",
        true,
        [&events] { events.emplace_back("install:second"); return true; },
        [&events] { events.emplace_back("remove:second"); }});
    transaction.add({
        "game.failed",
        true,
        [&events] { events.emplace_back("install:failed"); return false; },
        [&events] { events.emplace_back("remove:failed"); }});

    std::string error;
    REQUIRE_FALSE(transaction.commit(error));
    REQUIRE(error == "game.failed: hook installation failed");
    REQUIRE(events == std::vector<std::string>{
                          "install:first",
                          "install:second",
                          "install:failed",
                          "remove:second",
                          "remove:first"});
    REQUIRE_FALSE(transaction.committed());
}

TEST_CASE("match hook preserves x86 lifecycle ordering and removes cleanly", "[hooks]") {
    std::vector<std::string> calls;
    FakeGameScreen screen{&calls};
    int screen_data = 1;
    btd5loader::runtime::MatchHook hook;
    std::string error;
    REQUIRE(hook.install(
        reinterpret_cast<void*>(&fake_game_screen_init),
        reinterpret_cast<void*>(&fake_game_screen_uninit),
        [&calls, &screen](void* instance) { REQUIRE(instance == &screen); calls.emplace_back("starting"); },
        [&calls, &screen](void* instance) { REQUIRE(instance == &screen); calls.emplace_back("started"); },
        [&calls, &screen](void* instance) { REQUIRE(instance == &screen); calls.emplace_back("ending"); },
        [&calls, &screen](void* instance) { REQUIRE(instance == &screen); calls.emplace_back("ended"); },
        error));
    REQUIRE(error.empty());
    REQUIRE(hook.installed());

    fake_game_screen_init(&screen, nullptr, &screen_data);
    REQUIRE(calls == std::vector<std::string>{"starting", "original", "started"});
    fake_game_screen_uninit(&screen, nullptr);
    REQUIRE(calls == std::vector<std::string>{
                         "starting",
                         "original",
                         "started",
                         "ending",
                         "original_uninit",
                         "ended"});

    hook.remove();
    REQUIRE_FALSE(hook.installed());
    calls.clear();
    fake_game_screen_init(&screen, nullptr, &screen_data);
    fake_game_screen_uninit(&screen, nullptr);
    REQUIRE(calls == std::vector<std::string>{"original", "original_uninit"});
}

TEST_CASE("round hook captures the native receiver before start dispatch", "[hooks]") {
    FakeRoundState round;
    int number = 1;
    void* observed = nullptr;
    btd5loader::runtime::RoundHook hook;
    std::string error;
    REQUIRE(hook.install(
        reinterpret_cast<void*>(&fake_round_start),
        [&round, &observed](void* candidate) {
            REQUIRE(round.started == 0);
            observed = candidate;
        },
        error));
    fake_round_start(&round, nullptr, &number);
    REQUIRE(observed == &round);
    REQUIRE(round.started == 1);

    hook.remove();
    observed = nullptr;
    fake_round_start(&round, nullptr, &number);
    REQUIRE(observed == nullptr);
    REQUIRE(round.started == 2);
}

TEST_CASE("tower placement hook fires before manager ownership", "[hooks]") {
    std::vector<std::string> calls;
    FakeTowerManager manager{nullptr, &calls};
    int tower = 1;
    btd5loader::runtime::TowerPlacementHook hook;
    std::string error;
    REQUIRE(hook.install(
        reinterpret_cast<void*>(&fake_tower_placement),
        [&manager, &calls, &tower](void* const candidate) {
            REQUIRE(candidate == &tower);
            REQUIRE(manager.placed == nullptr);
            calls.emplace_back("placing");
        },
        error));
    REQUIRE(error.empty());
    REQUIRE(hook.installed());

    fake_tower_placement(&manager, nullptr, &tower);
    REQUIRE(manager.placed == &tower);
    REQUIRE(calls == std::vector<std::string>{"placing", "original"});

    hook.remove();
    REQUIRE_FALSE(hook.installed());
    manager.placed = nullptr;
    calls.clear();
    fake_tower_placement(&manager, nullptr, &tower);
    REQUIRE(manager.placed == &tower);
    REQUIRE(calls == std::vector<std::string>{"original"});
}

TEST_CASE("object lifetime hook reports constructed objects and pre-destruction", "[hooks]") {
    using btd5loader::runtime::GameObjectKind;
    std::vector<std::string> calls;
    FakeLifetimeObject attack{&calls};
    FakeLifetimeObject projectile{&calls};
    btd5loader::runtime::ObjectLifetimeHook hook;
    btd5loader::runtime::ObjectLifetimeHook::Targets targets;
    targets.attack_constructor = reinterpret_cast<void*>(&fake_attack_constructor);
    targets.attack_destructor = reinterpret_cast<void*>(&fake_attack_destructor);
    targets.projectile_constructors = {
        reinterpret_cast<void*>(&fake_projectile_constructor_primary),
        reinterpret_cast<void*>(&fake_projectile_constructor_secondary),
        reinterpret_cast<void*>(&fake_projectile_constructor_tertiary)};
    targets.projectile_destructor = reinterpret_cast<void*>(&fake_projectile_destructor);
    targets.tower_destructor = reinterpret_cast<void*>(&fake_tower_destructor);
    targets.bloon_destructor = reinterpret_cast<void*>(&fake_bloon_destructor);
    const auto label = [](const GameObjectKind kind) {
        return std::string(btd5loader::runtime::game_object_kind_name(kind));
    };
    std::string error;
    REQUIRE(hook.install(
        targets,
        [&](const GameObjectKind kind, void* const object, const void* const caller) {
            auto* const fake = static_cast<FakeLifetimeObject*>(object);
            REQUIRE(fake->constructed);
            REQUIRE(caller != nullptr);
            calls.emplace_back("created:" + label(kind));
        },
        [&](const GameObjectKind kind, void* const object) {
            auto* const fake = static_cast<FakeLifetimeObject*>(object);
            REQUIRE(fake->constructed);
            calls.emplace_back("destroying:" + label(kind));
        },
        error));
    REQUIRE(error.empty());
    REQUIRE(hook.installed());

    REQUIRE(fake_attack_constructor(&attack, nullptr, 1, 2) == &attack);
    fake_attack_destructor(&attack, nullptr);
    REQUIRE(fake_projectile_constructor_primary(&projectile, nullptr, 1, 2, 3, 4) == &projectile);
    fake_projectile_destructor(&projectile, nullptr);
    REQUIRE(fake_projectile_constructor_secondary(&projectile, nullptr, 1, 2, 3, 4) == &projectile);
    fake_projectile_destructor(&projectile, nullptr);
    REQUIRE(fake_projectile_constructor_tertiary(&projectile, nullptr, 1, 2, 3, 4, 5, 6) ==
            &projectile);
    fake_projectile_destructor(&projectile, nullptr);
    REQUIRE(calls == std::vector<std::string>{
                         "attack_constructor", "created:attack",
                         "destroying:attack", "attack_destructor",
                         "projectile_primary", "created:projectile",
                         "destroying:projectile", "projectile_destructor",
                         "projectile_secondary", "created:projectile",
                         "destroying:projectile", "projectile_destructor",
                         "projectile_tertiary", "created:projectile",
                         "destroying:projectile", "projectile_destructor"});

    FakeLifetimeObject tower{&calls, true};
    FakeLifetimeObject bloon{&calls, true};
    calls.clear();
    fake_tower_destructor(&tower, nullptr);
    fake_bloon_destructor(&bloon, nullptr);
    REQUIRE(calls == std::vector<std::string>{
                         "destroying:tower", "tower_destructor",
                         "destroying:bloon", "bloon_destructor"});

    btd5loader::runtime::ObjectLifetimeHook second;
    REQUIRE_FALSE(second.install(
        targets, [](GameObjectKind, void*, const void*) {}, [](GameObjectKind, void*) {}, error));
    REQUIRE_FALSE(error.empty());

    hook.remove();
    REQUIRE_FALSE(hook.installed());
    calls.clear();
    (void)fake_attack_constructor(&attack, nullptr, 1, 2);
    fake_attack_destructor(&attack, nullptr);
    (void)fake_projectile_constructor_tertiary(&projectile, nullptr, 1, 2, 3, 4, 5, 6);
    fake_projectile_destructor(&projectile, nullptr);
    fake_tower_destructor(&tower, nullptr);
    fake_bloon_destructor(&bloon, nullptr);
    REQUIRE(calls == std::vector<std::string>{
                         "attack_constructor", "attack_destructor",
                         "projectile_tertiary", "projectile_destructor",
                         "tower_destructor", "bloon_destructor"});
}

TEST_CASE("object lifetime hook rejects missing targets without hooking", "[hooks]") {
    btd5loader::runtime::ObjectLifetimeHook hook;
    btd5loader::runtime::ObjectLifetimeHook::Targets targets;
    targets.attack_constructor = reinterpret_cast<void*>(&fake_attack_constructor);
    std::string error;
    REQUIRE_FALSE(hook.install(
        targets,
        [](btd5loader::runtime::GameObjectKind, void*, const void*) {},
        [](btd5loader::runtime::GameObjectKind, void*) {},
        error));
    REQUIRE_FALSE(error.empty());
    REQUIRE_FALSE(hook.installed());
    std::vector<std::string> calls;
    FakeLifetimeObject attack{&calls};
    (void)fake_attack_constructor(&attack, nullptr, 1, 2);
    REQUIRE(calls == std::vector<std::string>{"attack_constructor"});
}

TEST_CASE("teardown ledger groups survivors by site and settles their fate", "[objects]") {
    using btd5loader::runtime::GameObjectKind;
    btd5loader::runtime::TeardownLedger ledger;
    REQUIRE(ledger.settle().empty());
    REQUIRE(ledger.capture({}) == "teardown_survivors none");
    REQUIRE(ledger.settle().empty());

    int prototypes[3]{};
    int clone{};
    int projectile{};
    int destroyed_in_match{};
    for (int& prototype : prototypes) ledger.record_site(&prototype, 0x1F3773);
    ledger.record_site(&clone, 0x2E6D02);
    ledger.record_site(&destroyed_in_match, 0x2E6D02);
    ledger.forget(&destroyed_in_match);
    REQUIRE(ledger.capture({{GameObjectKind::Attack, &prototypes[0]},
                {GameObjectKind::Attack, &prototypes[1]},
                {GameObjectKind::Attack, &prototypes[2]},
                {GameObjectKind::Attack, &clone},
                {GameObjectKind::Projectile, &projectile}}) ==
            "teardown_survivors attack@0x1F3773=3;attack@0x2E6D02=1;projectile@unknown=1");

    ledger.destroyed(&clone);
    ledger.destroyed(&clone);
    ledger.constructed(&prototypes[0]);
    ledger.destroyed(&destroyed_in_match);
    REQUIRE(ledger.settle() ==
            "teardown_fate attack:destroyed=1,reused=1,live=2;projectile:destroyed=0,reused=0,live=1");
    REQUIRE(ledger.settle().empty());

    // Sites are cleared at capture, so a later scene starts without stale sites.
    REQUIRE(ledger.capture({{GameObjectKind::Attack, &prototypes[1]}}) ==
            "teardown_survivors attack@unknown=1");
    ledger.destroyed(&prototypes[1]);
    REQUIRE(ledger.settle() == "teardown_fate attack:destroyed=1,reused=0,live=0");
}

TEST_CASE("teardown ledger keeps outstanding survivors across settlements", "[objects]") {
    using btd5loader::runtime::GameObjectKind;
    btd5loader::runtime::TeardownLedger ledger;
    REQUIRE(ledger.outstanding() == "teardown_outstanding none");

    int first[2]{};
    int second[2]{};
    int projectile{};
    (void)ledger.capture({{GameObjectKind::Attack, &first[0]}, {GameObjectKind::Attack, &first[1]}});
    REQUIRE(ledger.outstanding() == "teardown_outstanding attack=2");
    (void)ledger.settle();

    // A later match leaves new survivors while the earlier ones stay live.
    (void)ledger.capture({{GameObjectKind::Attack, &second[0]}, {GameObjectKind::Attack, &second[1]},
        {GameObjectKind::Projectile, &projectile}});
    REQUIRE(ledger.outstanding() == "teardown_outstanding attack=4;projectile=1");
    (void)ledger.settle();

    // Destruction or address reuse after settlement still clears a survivor,
    // and recapturing the same address does not count it twice.
    ledger.destroyed(&first[0]);
    ledger.constructed(&first[1]);
    (void)ledger.capture({{GameObjectKind::Attack, &second[0]}});
    REQUIRE(ledger.outstanding() == "teardown_outstanding attack=2;projectile=1");
}

TEST_CASE("runtime logger keeps one handle, caps size, and survives external clearing", "[logging]") {
    const auto root = std::filesystem::temp_directory_path() /
                      (L"btd5ml-logger-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(root);
    std::wstring previous(32767, L'\0');
    const DWORD previous_length =
        GetEnvironmentVariableW(L"BTD5ML_DATA_ROOT", previous.data(), static_cast<DWORD>(previous.size()));
    previous.resize(previous_length);
    REQUIRE(SetEnvironmentVariableW(L"BTD5ML_DATA_ROOT", root.c_str()) != FALSE);

    constexpr std::uint64_t cap = 4096;
    const auto current = root / L"logs" / L"runtime.jsonl";
    const auto rotated = root / L"logs" / L"runtime.1.jsonl";
    {
        btd5loader::runtime::Logger logger(cap);
        REQUIRE(logger.initialize());
        REQUIRE(logger.path() == current);
        logger.info("test", "first \"quoted\" line");
        REQUIRE(read_test_file(current).find("first \\\"quoted\\\" line") != std::string::npos);

        // Readers that share read/write/delete can open the file while it is held.
        {
            std::ifstream reader(current, std::ios::binary);
            REQUIRE(reader.good());
        }

        const std::string payload(200, 'x');
        for (int index = 0; index < 100; ++index) {
            logger.info("test", payload + std::to_string(index));
        }
        REQUIRE(std::filesystem::exists(rotated));
        REQUIRE(std::filesystem::file_size(current) <= cap);
        REQUIRE(std::filesystem::file_size(rotated) <= cap);
        const auto latest = read_test_file(current);
        REQUIRE(latest.find(payload + "99") != std::string::npos);
        REQUIRE(latest.back() == '\n');
        REQUIRE(latest.front() == '{');

        // The manager clears by truncating in place; later records still append.
        {
            std::ofstream clear(current, std::ios::binary | std::ios::trunc);
            REQUIRE(clear.good());
        }
        logger.error("test", "after clear");
        const auto cleared = read_test_file(current);
        REQUIRE(cleared.rfind("{\"timestamp\":", 0) == 0);
        REQUIRE(cleared.find("after clear") != std::string::npos);
        REQUIRE(std::count(cleared.begin(), cleared.end(), '\n') == 1);
    }

    if (previous_length == 0) {
        SetEnvironmentVariableW(L"BTD5ML_DATA_ROOT", nullptr);
    } else {
        SetEnvironmentVariableW(L"BTD5ML_DATA_ROOT", previous.c_str());
    }
    std::filesystem::remove_all(root);
}

TEST_CASE("cash capture copies native balances and survives nested updates", "[hooks][cash]") {
    struct FakeMoneyEvent final {
        void* vtable{};
        std::uint32_t padding{};
        double balance{};
    };
    static_assert(offsetof(FakeMoneyEvent, balance) ==
                  btd5loader::runtime::CashCaptureStack::kBalanceOffset);
    btd5loader::runtime::CashCaptureStack stack;
    REQUIRE(stack.capture(nullptr) == nullptr);

    FakeMoneyEvent outer{nullptr, 0, 650.0};
    void* const outer_token = stack.capture(&outer);
    REQUIRE(outer_token != nullptr);
    outer.balance = -1.0;  // The game may destroy or reuse the event during dispatch.
    REQUIRE(stack.value(outer_token) == 650.0);

    FakeMoneyEvent inner{nullptr, 0, 425.5};
    void* const inner_token = stack.capture(&inner);
    REQUIRE(stack.value(inner_token) == 425.5);
    REQUIRE_FALSE(stack.value(outer_token).has_value());
    stack.release(outer_token);  // Out-of-order release is ignored.
    REQUIRE(stack.depth() == 2);
    stack.release(inner_token);
    REQUIRE(stack.value(outer_token) == 650.0);
    stack.release(outer_token);
    REQUIRE(stack.depth() == 0);

    FakeMoneyEvent invalid{nullptr, 0, std::numeric_limits<double>::infinity()};
    REQUIRE(stack.capture(&invalid) == nullptr);
    invalid.balance = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(stack.capture(&invalid) == nullptr);

    std::vector<void*> tokens;
    FakeMoneyEvent deep{nullptr, 0, 1.0};
    for (std::size_t index = 0; index < btd5loader::runtime::CashCaptureStack::kCapacity; ++index) {
        tokens.push_back(stack.capture(&deep));
        REQUIRE(tokens.back() != nullptr);
    }
    REQUIRE(stack.capture(&deep) == nullptr);
    stack.release(nullptr);
    for (auto token = tokens.rbegin(); token != tokens.rend(); ++token) {
        stack.release(*token);
    }
    REQUIRE(stack.depth() == 0);
}

TEST_CASE("lifecycle sample reads the cash balance payload", "[samples][lua][cash]") {
    const auto source = std::filesystem::path(BTD5ML_SOURCE_DIR) /
                        L"samples" / L"lifecycle-mod";
    std::vector<std::string> logs;
    btd5loader::runtime::LuaModOptions options;
    options.mod_id = "sample.lifecycle";
    options.storage_directory = std::filesystem::temp_directory_path() /
                                (L"btd5ml-cash-sample-" + std::to_wstring(GetCurrentProcessId()));
    options.resource_directory = source;
    options.configuration = {{"greeting", "Hello from Lua"}};
    options.log = [&logs](const std::string_view, const std::string_view message) {
        logs.emplace_back(message);
    };
    btd5loader::runtime::LuaMod mod(std::move(options));
    REQUIRE(mod.load_script(read_test_file(source / L"lua" / L"main.lua"), "lua/main.lua"));
    REQUIRE(mod.dispatch_event("cash.changing", {{"cash", 650.0}}).succeeded);
    REQUIRE(mod.dispatch_event("cash.changed", {{"cash", 650.0}}).succeeded);
    REQUIRE(mod.last_error().empty());
    REQUIRE(std::find(logs.begin(), logs.end(),
                "Lifecycle Sample observed cash.changing cash=650") != logs.end());
    REQUIRE(std::find(logs.begin(), logs.end(),
                "Lifecycle Sample observed cash.changed cash=650") != logs.end());

    // A read-only payload ignores an attempted handler mutation.
    btd5loader::runtime::LuaModOptions writer_options;
    writer_options.mod_id = "sample.cash-writer";
    btd5loader::runtime::LuaMod writer(std::move(writer_options));
    REQUIRE(writer.load_script(R"lua(
        btd5.events.on("cash.changing", function(event) event.cash = 1e9 end)
    )lua", "writer.lua"));
    const auto result = writer.dispatch_event("cash.changing", {{"cash", 650.0}});
    REQUIRE(result.succeeded);
}

TEST_CASE("tower purchase hook routes cancellation through the native rejection branch", "[hooks]") {
    // Mirrors the supported build's placement purchase sequence:
    // call free-token check; test al,al; jne commit; push edi; mov ecx,ebx;
    // call cost check; test al,al; jne commit; reject: xor eax,eax.
    std::array<std::uint8_t, 0x31> routine{
        0x55, 0x8B, 0xEC, 0x53, 0x57, 0x8B, 0xD9, 0x8B, 0x7D, 0x08,
        0x57, 0x8B, 0xCB, 0xE8, 0, 0, 0, 0, 0x84, 0xC0, 0x75, 0x10,
        0x57, 0x8B, 0xCB, 0xE8, 0, 0, 0, 0, 0x84, 0xC0, 0x75, 0x04,
        0x33, 0xC0, 0xEB, 0x05, 0xB8, 0x01, 0x00, 0x00, 0x00,
        0x5F, 0x5B, 0x5D, 0xC2, 0x04, 0x00};
    constexpr std::size_t free_call = 0x0D;
    constexpr std::size_t cost_call = 0x19;
    static_assert(cost_call - free_call == btd5loader::runtime::TowerPurchaseHook::kCostCheckCallOffset);
    auto* const code = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, routine.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    REQUIRE(code != nullptr);
    if (code == nullptr) return;
    std::memcpy(code, routine.data(), routine.size());
    const auto link = [code](const std::size_t call, const void* const target) {
        const auto relative = static_cast<std::int32_t>(
            reinterpret_cast<std::uintptr_t>(target) -
            reinterpret_cast<std::uintptr_t>(code + call + 5));
        std::memcpy(code + call + 1, &relative, sizeof(relative));
    };
    link(free_call, reinterpret_cast<const void*>(&fake_free_token_check));
    link(cost_call, reinterpret_cast<const void*>(&fake_cost_check));
    using Purchase = std::uint32_t(__thiscall*)(void*, void*);
    const auto purchase = reinterpret_cast<Purchase>(code);
    const std::vector<std::uint8_t> pristine(code, code + routine.size());

    FakePurchaseScreen screen;
    int tower = 0;
    int other = 0;
    bool cancel = true;
    std::vector<void*> seen;
    btd5loader::runtime::TowerPurchaseHook hook;
    std::string error;
    REQUIRE(hook.install(
        code + free_call,
        [&](void* const candidate) {
            seen.push_back(candidate);
            REQUIRE(screen.free_checks == 0);
            return cancel && candidate == &tower;
        },
        error));
    REQUIRE(error.empty());

    // Cancelled: neither original runs, no cash is spent, and placement is rejected.
    REQUIRE(purchase(&screen, &tower) == 0);
    REQUIRE(screen.free_checks == 0);
    REQUIRE(screen.cost_checks == 0);
    REQUIRE(screen.cash == 100);

    // Allowed: both originals run in order and the cost is charged.
    cancel = false;
    REQUIRE(purchase(&screen, &tower) == 1);
    REQUIRE(screen.free_checks == 1);
    REQUIRE(screen.cost_checks == 1);
    REQUIRE(screen.cash == 60);

    // A free token commits without reaching the cost check.
    screen.free_checks = 0;
    screen.cost_checks = 0;
    screen.free_token = true;
    REQUIRE(purchase(&screen, &other) == 1);
    REQUIRE(screen.free_checks == 1);
    REQUIRE(screen.cost_checks == 0);

    // Native unaffordable rejection is preserved when Lua allows the purchase.
    screen.free_checks = 0;
    screen.affordable = false;
    REQUIRE(purchase(&screen, &other) == 0);
    REQUIRE(screen.free_checks == 1);
    REQUIRE(screen.cost_checks == 1);
    REQUIRE(seen == std::vector<void*>{&tower, &tower, &other, &other});

    btd5loader::runtime::TowerPurchaseHook second;
    REQUIRE_FALSE(second.install(code + free_call, [](void*) { return false; }, error));

    hook.remove();
    REQUIRE(std::equal(pristine.begin(), pristine.end(), code));
    seen.clear();
    screen = {};
    REQUIRE(purchase(&screen, &tower) == 1);
    REQUIRE(seen.empty());
    REQUIRE(screen.cash == 60);

    // An unexpected instruction shape is refused without modifying code.
    code[free_call + 7] = 0x74;
    error.clear();
    REQUIRE_FALSE(hook.install(code + free_call, [](void*) { return true; }, error));
    REQUIRE_FALSE(error.empty());
    REQUIRE_FALSE(hook.installed());
    VirtualFree(code, 0, MEM_RELEASE);
}

TEST_CASE("tower upgrade hook propagates cancellation and restores its commit boundary", "[hooks]") {
    constexpr std::array<std::byte, 16> instructions{
        std::byte{0x84}, std::byte{0xC0}, std::byte{0x0F}, std::byte{0x84},
        std::byte{0x08}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x56}, std::byte{0x8B}, std::byte{0xCF}, std::byte{0xE8},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
    auto* const code = static_cast<std::byte*>(VirtualAlloc(
        nullptr, instructions.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    REQUIRE(code != nullptr);
    if (code == nullptr) return;
    std::memcpy(code, instructions.data(), instructions.size());

    bool cancel = false;
    bool throw_on_dispatch = false;
    int tower = 1;
    void* observed = nullptr;
    btd5loader::runtime::TowerUpgradeHook hook;
    std::string error;
    REQUIRE(hook.install(
        code + 8,
        [&cancel, &throw_on_dispatch, &observed](void* const candidate) {
            observed = candidate;
            if (throw_on_dispatch) {
                throw std::runtime_error("fixture failure");
            }
            return cancel;
        },
        error));
    REQUIRE(error.empty());
    REQUIRE(hook.installed());

    REQUIRE_FALSE(btd5loader::runtime::TowerUpgradeHook::dispatch(&tower));
    REQUIRE(observed == &tower);
    cancel = true;
    REQUIRE(btd5loader::runtime::TowerUpgradeHook::dispatch(&tower));
    throw_on_dispatch = true;
    REQUIRE_FALSE(btd5loader::runtime::TowerUpgradeHook::dispatch(&tower));

    hook.remove();
    REQUIRE_FALSE(hook.installed());
    REQUIRE(std::memcmp(code + 8, instructions.data() + 8, 8) == 0);
#pragma warning(suppress : 6001)  // Guarded above; Catch2 REQUIRE confuses code analysis.
    REQUIRE(VirtualFree(code, 0, MEM_RELEASE) != FALSE);
}

TEST_CASE("tower sale hook propagates cancellation and restores its commit boundary", "[hooks]") {
    constexpr std::array<std::byte, 14> instructions{
        std::byte{0x84}, std::byte{0xC0}, std::byte{0x0F}, std::byte{0x85},
        std::byte{0x06}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x8D}, std::byte{0xBB}, std::byte{0x14}, std::byte{0xFF},
        std::byte{0xFF}, std::byte{0xFF}};
    auto* const code = static_cast<std::byte*>(VirtualAlloc(
        nullptr, instructions.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    REQUIRE(code != nullptr);
    if (code == nullptr) return;
    std::memcpy(code, instructions.data(), instructions.size());

    bool cancel = false;
    bool throw_on_dispatch = false;
    int tower = 1;
    void* observed = nullptr;
    btd5loader::runtime::TowerSaleHook hook;
    std::string error;
    REQUIRE(hook.install(
        code + 8,
        [&cancel, &throw_on_dispatch, &observed](void* const candidate) {
            observed = candidate;
            if (throw_on_dispatch) {
                throw std::runtime_error("fixture failure");
            }
            return cancel;
        },
        error));
    REQUIRE(error.empty());
    REQUIRE(hook.installed());

    REQUIRE_FALSE(btd5loader::runtime::TowerSaleHook::dispatch(&tower));
    REQUIRE(observed == &tower);
    cancel = true;
    REQUIRE(btd5loader::runtime::TowerSaleHook::dispatch(&tower));
    throw_on_dispatch = true;
    REQUIRE_FALSE(btd5loader::runtime::TowerSaleHook::dispatch(&tower));

    hook.remove();
    REQUIRE_FALSE(hook.installed());
    REQUIRE(std::memcmp(code + 8, instructions.data() + 8, 6) == 0);
#pragma warning(suppress : 6001)  // Guarded above; Catch2 REQUIRE confuses code analysis.
    REQUIRE(VirtualFree(code, 0, MEM_RELEASE) != FALSE);
}

// A cdecl function (bloon, damage) that runs the damage function's commit
// sequence around the 17 patched bytes at offset kFakeLethalHitOffset, then
// stores XMM0 at bloon+0x230 so a test can see what the continuation observes.
constexpr std::size_t kFakeLethalHitOffset = 31;
constexpr std::array<std::uint8_t, 65> kFakeDamageCommit{
    0x55, 0x8B, 0xEC, 0x57, 0x8B, 0x7D, 0x08,                    // push ebp; mov ebp,esp; push edi; mov edi,[ebp+8]
    0xF3, 0x0F, 0x10, 0x4D, 0x0C,                                // movss xmm1,[ebp+0xC]
    0xF3, 0x0F, 0x10, 0x87, 0x24, 0x02, 0x00, 0x00,              // movss xmm0,[edi+0x224]
    0x0F, 0x57, 0xD2,                                            // xorps xmm2,xmm2
    0xF3, 0x0F, 0x5C, 0xC1,                                      // subss xmm0,xmm1
    0x0F, 0x2F, 0xD0,                                            // comiss xmm2,xmm0
    0x90,                                                        // nop (keeps the patch site aligned with the game)
    0xF3, 0x0F, 0x11, 0x87, 0x24, 0x02, 0x00, 0x00,              // movss [edi+0x224],xmm0
    0x72, 0x07,                                                  // jb +7
    0xC6, 0x87, 0x20, 0x02, 0x00, 0x00, 0x01,                    // mov byte [edi+0x220],1
    0xF3, 0x0F, 0x11, 0x87, 0x30, 0x02, 0x00, 0x00,              // movss [edi+0x230],xmm0
    0x5F, 0x5D, 0xC3,                                            // pop edi; pop ebp; ret
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC};

constexpr std::array<std::uint8_t, 13> kFakeLeakCommit{
    0x0F, 0x82, 0x07, 0x00, 0x00, 0x00, 0x6A, 0x08, 0xE8, 0x00, 0x00, 0x00, 0x00};

struct FakeBloon final {
    alignas(16) std::array<std::byte, 0x240> bytes{};
    float health() const noexcept { float value{}; std::memcpy(&value, bytes.data() + 0x224, 4); return value; }
    void set_health(const float value) noexcept { std::memcpy(bytes.data() + 0x224, &value, 4); }
    float continuation() const noexcept { float value{}; std::memcpy(&value, bytes.data() + 0x230, 4); return value; }
    bool popped() const noexcept { return bytes[0x220] != std::byte{0}; }
};

std::byte* allocate_code(const std::uint8_t* const source, const std::size_t size) {
    auto* const code = static_cast<std::byte*>(
        VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (code != nullptr) std::memcpy(code, source, size);
    return code;
}

// Spawn-rejection fakes: the manager's deferred-deletion queue push and the
// bloon's status-effect removal, both __thiscall with one stack argument.
std::vector<void*> g_fake_queued;
std::vector<std::pair<void*, char>> g_fake_stripped;

__declspec(noinline) void __fastcall fake_queue_push(void* const queue, void*, void* const* const value) {
    if (queue != nullptr && value != nullptr) g_fake_queued.push_back(*value);
}

__declspec(noinline) void __fastcall fake_strip_effects(void* const bloon, void*, const char expired_only) {
    if (bloon != nullptr) g_fake_stripped.emplace_back(bloon, expired_only);
}

// Never used as an emitter: only fixtures that call through kFakeSpawnCaller
// set a real return address.
const btd5loader::runtime::BloonActionHook::SpawnRejection kFakeSpawnRejection{
    reinterpret_cast<void*>(0x1), reinterpret_cast<void*>(&fake_queue_push),
    reinterpret_cast<void*>(&fake_strip_effects)};

// A fake manager: just large enough for the deletion queue at +0x187C.
struct FakeBloonManager final {
    alignas(4) std::array<std::byte, 0x1890> bytes{};
    std::vector<void*>* spawned{};
};

__declspec(noinline) void __fastcall fake_bloon_spawn(void* const manager, void*, void* const bloon) {
    auto* const fake = static_cast<FakeBloonManager*>(manager);
    if (fake != nullptr && fake->spawned != nullptr) fake->spawned->push_back(bloon);
}

__declspec(noinline) void __fastcall fake_boss_spawn(void* const manager, void*, void* const bloon) {
    auto* const fake = static_cast<FakeBloonManager*>(manager);
    if (fake != nullptr && fake->spawned != nullptr) fake->spawned->insert(fake->spawned->begin(), bloon);
}

// cdecl (manager, bloon, spawn): calls spawn as __thiscall, like the round
// emitter's virtual call; its return address is code + kFakeSpawnCallerReturn.
constexpr std::size_t kFakeSpawnCallerReturn = 14;
constexpr std::array<std::uint8_t, 16> kFakeSpawnCaller{
    0x55, 0x8B, 0xEC,  // push ebp; mov ebp,esp
    0x8B, 0x4D, 0x08,  // mov ecx,[ebp+8]
    0xFF, 0x75, 0x0C,  // push [ebp+0xC]
    0x8B, 0x45, 0x10,  // mov eax,[ebp+0x10]
    0xFF, 0xD0,        // call eax
    0x5D, 0xC3};       // pop ebp; ret

TEST_CASE("bloon leak hook propagates cancellation and restores its commit boundary", "[hooks]") {
    constexpr std::array<std::byte, 13> instructions{
        std::byte{0x0F}, std::byte{0x82}, std::byte{0x07}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x6A}, std::byte{0x08},
        std::byte{0xE8}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}};
    auto* const code = static_cast<std::byte*>(VirtualAlloc(
        nullptr, instructions.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    REQUIRE(code != nullptr);
    if (code == nullptr) return;
    std::memcpy(code, instructions.data(), instructions.size());

    auto* const lethal = allocate_code(kFakeDamageCommit.data(), kFakeDamageCommit.size());
    REQUIRE(lethal != nullptr);
    if (lethal == nullptr) return;

    bool cancel = false;
    bool throw_on_dispatch = false;
    int bloon = 1;
    void* observed = nullptr;
    btd5loader::runtime::BloonActionHook hook;
    std::string error;
    REQUIRE(hook.install(
        reinterpret_cast<void*>(&fake_game_screen_init),
        reinterpret_cast<void*>(&fake_game_screen_uninit),
        kFakeSpawnRejection,
        lethal + kFakeLethalHitOffset,
        code + 6,
        [](void*, bool) { return false; },
        [](void*) { return false; },
        [&cancel, &throw_on_dispatch, &observed](void* const candidate) {
            observed = candidate;
            if (throw_on_dispatch) {
                throw std::runtime_error("fixture failure");
            }
            return cancel;
        },
        error));
    REQUIRE(error.empty());
    REQUIRE(hook.installed());

    REQUIRE_FALSE(btd5loader::runtime::BloonActionHook::dispatch_leaking(&bloon));
    REQUIRE(observed == &bloon);
    cancel = true;
    REQUIRE(btd5loader::runtime::BloonActionHook::dispatch_leaking(&bloon));
    throw_on_dispatch = true;
    REQUIRE_FALSE(btd5loader::runtime::BloonActionHook::dispatch_leaking(&bloon));

    hook.remove();
    REQUIRE_FALSE(hook.installed());
    REQUIRE(std::memcmp(code + 6, instructions.data() + 6, 7) == 0);
#pragma warning(suppress : 6001)  // Guarded above; Catch2 REQUIRE confuses code analysis.
    REQUIRE(VirtualFree(code, 0, MEM_RELEASE) != FALSE);
    REQUIRE(VirtualFree(lethal, 0, MEM_RELEASE) != FALSE);
}

TEST_CASE("bloon spawn rejection decodes its helpers from verified call sites", "[hooks]") {
    // Primary spawn: the push_back call site at +0x31 and the push_back body.
    std::array<std::byte, 0x80> spawn{};
    constexpr std::array<std::uint8_t, 7> push_site{0x8D, 0x4F, 0x04, 0x89, 0x5D, 0x08, 0xE8};
    constexpr std::array<std::uint8_t, 6> push_prologue{0x55, 0x8B, 0xEC, 0x56, 0x8B, 0xF1};
    std::memcpy(spawn.data() + 0x31, push_site.data(), push_site.size());
    const std::int32_t push_relative = 0x60 - 0x3C;
    std::memcpy(spawn.data() + 0x38, &push_relative, sizeof(push_relative));
    std::memcpy(spawn.data() + 0x60, push_prologue.data(), push_prologue.size());
    // Pop routine: the effect-removal call site at +0x14D and its body.
    std::array<std::byte, 0x180> pop{};
    constexpr std::array<std::uint8_t, 5> strip_site{0x6A, 0x01, 0x8B, 0xCF, 0xE8};
    constexpr std::array<std::uint8_t, 5> strip_prologue{0x55, 0x8B, 0xEC, 0x6A, 0xFF};
    std::memcpy(pop.data() + 0x14D, strip_site.data(), strip_site.size());
    const std::int32_t strip_relative = 0x10 - 0x156;
    std::memcpy(pop.data() + 0x152, &strip_relative, sizeof(strip_relative));
    std::memcpy(pop.data() + 0x10, strip_prologue.data(), strip_prologue.size());
    // Emitter: mov eax,[ecx] / call [eax+4], then the return address.
    std::array<std::byte, 8> emitter{};
    constexpr std::array<std::uint8_t, 5> emitter_call{0x8B, 0x01, 0xFF, 0x50, 0x04};
    std::memcpy(emitter.data(), emitter_call.data(), emitter_call.size());

    using btd5loader::runtime::BloonActionHook;
    BloonActionHook::SpawnRejection rejection;
    std::string error;
    REQUIRE(BloonActionHook::resolve_rejection(spawn.data(), pop.data(), emitter.data() + 5,
                                               rejection, error));
    REQUIRE(rejection.emitter_return == emitter.data() + 5);
    REQUIRE(rejection.queue_push == spawn.data() + 0x60);
    REQUIRE(rejection.strip_effects == pop.data() + 0x10);

    // Each anchor is checked: a wrong byte refuses the whole rejection.
    emitter[3] = std::byte{0x10};
    REQUIRE_FALSE(BloonActionHook::resolve_rejection(spawn.data(), pop.data(), emitter.data() + 5,
                                                     rejection, error));
    REQUIRE(error.find("emitter") != std::string::npos);
    REQUIRE(rejection.queue_push == nullptr);
    emitter[3] = std::byte{0x50};
    spawn[0x61] = std::byte{0x00};
    REQUIRE_FALSE(BloonActionHook::resolve_rejection(spawn.data(), pop.data(), emitter.data() + 5,
                                                     rejection, error));
    REQUIRE(error.find("push_back prologue") != std::string::npos);
    spawn[0x61] = std::byte{0x8B};
    pop[0x14F] = std::byte{0x90};
    REQUIRE_FALSE(BloonActionHook::resolve_rejection(spawn.data(), pop.data(), emitter.data() + 5,
                                                     rejection, error));
    REQUIRE(error.find("effect removal call") != std::string::npos);
    pop[0x14F] = std::byte{0x8B};
    REQUIRE(BloonActionHook::resolve_rejection(spawn.data(), pop.data(), emitter.data() + 5,
                                               rejection, error));
}

TEST_CASE("bloon spawn hook cancels only emitter spawns and queues them for deletion", "[hooks]") {
    auto* const lethal = allocate_code(kFakeDamageCommit.data(), kFakeDamageCommit.size());
    auto* const leak = allocate_code(kFakeLeakCommit.data(), kFakeLeakCommit.size());
    auto* const caller = allocate_code(kFakeSpawnCaller.data(), kFakeSpawnCaller.size());
    REQUIRE(lethal != nullptr);
    REQUIRE(leak != nullptr);
    REQUIRE(caller != nullptr);
    if (lethal == nullptr || leak == nullptr || caller == nullptr) return;
    using Caller = void(__cdecl*)(void*, void*, void*);
    const auto emit = reinterpret_cast<Caller>(caller);
    g_fake_queued.clear();
    g_fake_stripped.clear();

    std::vector<void*> spawned;
    FakeBloonManager manager;
    manager.spawned = &spawned;
    std::vector<std::pair<void*, bool>> dispatched;
    bool cancel = false;
    bool throw_on_dispatch = false;
    btd5loader::runtime::BloonActionHook::SpawnRejection rejection{
        caller + kFakeSpawnCallerReturn, reinterpret_cast<void*>(&fake_queue_push),
        reinterpret_cast<void*>(&fake_strip_effects)};
    btd5loader::runtime::BloonActionHook hook;
    std::string error;
    REQUIRE(hook.install(
        reinterpret_cast<void*>(&fake_bloon_spawn),
        reinterpret_cast<void*>(&fake_boss_spawn),
        rejection,
        lethal + kFakeLethalHitOffset,
        leak + 6,
        [&](void* const candidate, const bool cancellable) {
            dispatched.emplace_back(candidate, cancellable);
            if (throw_on_dispatch) throw std::runtime_error("fixture failure");
            return cancel;
        },
        [](void*) { return false; },
        [](void*) { return false; },
        error));
    INFO(error);
    REQUIRE(hook.installed());

    // An emitter spawn is cancellable and spawns normally when not cancelled.
    FakeBloon kept;
    emit(&manager, &kept, reinterpret_cast<void*>(&fake_bloon_spawn));
    REQUIRE(dispatched == std::vector<std::pair<void*, bool>>{{&kept, true}});
    REQUIRE(spawned == std::vector<void*>{&kept});
    REQUIRE(g_fake_queued.empty());

    // A cancelled emitter spawn never reaches the manager: it gets the native
    // removal flags, loses every status effect, and is queued for deletion.
    cancel = true;
    FakeBloon rejected;
    emit(&manager, &rejected, reinterpret_cast<void*>(&fake_bloon_spawn));
    REQUIRE(spawned.size() == 1);
    REQUIRE(rejected.bytes[0x220] == std::byte{1});
    REQUIRE(rejected.bytes[0x222] == std::byte{1});
    REQUIRE(g_fake_stripped == std::vector<std::pair<void*, char>>{{&rejected, char{0}}});
    REQUIRE(g_fake_queued == std::vector<void*>{&rejected});

    // Any other caller of primary spawn is not cancellable.
    FakeBloon ability;
    fake_bloon_spawn(&manager, nullptr, &ability);
    REQUIRE(dispatched.back() == std::pair<void*, bool>{&ability, false});
    REQUIRE(spawned.back() == &ability);

    // The boss spawner keeps the bloon, so the secondary entry never cancels.
    FakeBloon boss;
    emit(&manager, &boss, reinterpret_cast<void*>(&fake_boss_spawn));
    REQUIRE(dispatched.back() == std::pair<void*, bool>{&boss, false});
    REQUIRE(spawned.front() == &boss);

    // A deletion queue that does not look like a vector refuses the rejection,
    // and the bloon spawns untouched.
    const std::array<std::uintptr_t, 3> broken_queue{8, 4, 16};
    std::memcpy(manager.bytes.data() + 0x187C, broken_queue.data(), sizeof(broken_queue));
    FakeBloon refused;
    emit(&manager, &refused, reinterpret_cast<void*>(&fake_bloon_spawn));
    REQUIRE(spawned.back() == &refused);
    REQUIRE(refused.bytes[0x220] == std::byte{0});
    REQUIRE(g_fake_queued.size() == 1);
    manager.bytes = {};

    // A failing handler cannot cancel.
    throw_on_dispatch = true;
    FakeBloon failed;
    emit(&manager, &failed, reinterpret_cast<void*>(&fake_bloon_spawn));
    REQUIRE(spawned.back() == &failed);
    REQUIRE(g_fake_queued.size() == 1);

    hook.remove();
    REQUIRE_FALSE(hook.installed());
    // Unhooked, the emitter call reaches the original without dispatch.
    const auto calls = dispatched.size();
    FakeBloon after;
    emit(&manager, &after, reinterpret_cast<void*>(&fake_bloon_spawn));
    REQUIRE(dispatched.size() == calls);
    REQUIRE(spawned.back() == &after);
#pragma warning(suppress : 6001)  // Guarded above; Catch2 REQUIRE confuses code analysis.
    REQUIRE(VirtualFree(lethal, 0, MEM_RELEASE) != FALSE);
    REQUIRE(VirtualFree(leak, 0, MEM_RELEASE) != FALSE);
    REQUIRE(VirtualFree(caller, 0, MEM_RELEASE) != FALSE);
}

TEST_CASE("bloon lethal-hit hook dispatches popping once and cancels to pre-hit health", "[hooks]") {
    auto* const lethal = allocate_code(kFakeDamageCommit.data(), kFakeDamageCommit.size());
    auto* const leak = allocate_code(kFakeLeakCommit.data(), kFakeLeakCommit.size());
    REQUIRE(lethal != nullptr);
    REQUIRE(leak != nullptr);
    if (lethal == nullptr || leak == nullptr) return;
    using Damage = void(__cdecl*)(void*, float);
    const auto damage = reinterpret_cast<Damage>(lethal);

    // The unpatched sequence pops at zero or below and keeps XMM0 as the new health.
    FakeBloon bloon;
    bloon.set_health(2.0F);
    damage(&bloon, 5.0F);
    REQUIRE(bloon.popped());
    REQUIRE(bloon.health() == -3.0F);

    std::vector<void*> dispatched;
    bool cancel = false;
    bool throw_on_dispatch = false;
    float set_health = 0.0F;
    btd5loader::runtime::BloonActionHook hook;
    std::string error;
    REQUIRE(hook.install(
        reinterpret_cast<void*>(&fake_game_screen_init),
        reinterpret_cast<void*>(&fake_game_screen_uninit),
        kFakeSpawnRejection,
        lethal + kFakeLethalHitOffset,
        leak + 6,
        [](void*, bool) { return false; },
        [&](void* const candidate) {
            dispatched.push_back(candidate);
            if (set_health != 0.0F) static_cast<FakeBloon*>(candidate)->set_health(set_health);
            if (throw_on_dispatch) throw std::runtime_error("fixture failure");
            return cancel;
        },
        [](void*) { return false; },
        error));
    INFO(error);
    REQUIRE(hook.installed());

    // A non-lethal hit commits without dispatching.
    bloon = {};
    bloon.set_health(10.0F);
    damage(&bloon, 3.0F);
    REQUIRE(bloon.health() == 7.0F);
    REQUIRE(bloon.continuation() == 7.0F);
    REQUIRE_FALSE(bloon.popped());
    REQUIRE(dispatched.empty());

    // A lethal hit dispatches once and pops when not cancelled.
    bloon.set_health(2.0F);
    damage(&bloon, 5.0F);
    REQUIRE(dispatched == std::vector<void*>{&bloon});
    REQUIRE(bloon.popped());
    REQUIRE(bloon.health() == -3.0F);
    REQUIRE(bloon.continuation() == -3.0F);

    // A later hit on an already-popping bloon does not dispatch again.
    damage(&bloon, 1.0F);
    REQUIRE(dispatched.size() == 1);
    REQUIRE(bloon.health() == -4.0F);
    REQUIRE(bloon.popped());

    // A cancelled lethal hit keeps the exact pre-hit health, also in XMM0.
    bloon = {};
    bloon.set_health(2.5F);
    cancel = true;
    damage(&bloon, 5.0F);
    REQUIRE(dispatched.size() == 2);
    REQUIRE_FALSE(bloon.popped());
    REQUIRE(bloon.health() == 2.5F);
    REQUIRE(bloon.continuation() == 2.5F);

    // Health set by a cancelling handler is what the bloon keeps.
    set_health = 9.0F;
    damage(&bloon, 5.0F);
    REQUIRE_FALSE(bloon.popped());
    REQUIRE(bloon.health() == 9.0F);
    REQUIRE(bloon.continuation() == 9.0F);

    // A failing handler cannot cancel: the hit pops normally.
    set_health = 0.0F;
    throw_on_dispatch = true;
    bloon.set_health(1.0F);
    damage(&bloon, 5.0F);
    REQUIRE(bloon.popped());
    REQUIRE(bloon.health() == -4.0F);

    hook.remove();
    REQUIRE_FALSE(hook.installed());
    REQUIRE(std::memcmp(lethal, kFakeDamageCommit.data(), kFakeDamageCommit.size()) == 0);
    REQUIRE(std::memcmp(leak, kFakeLeakCommit.data(), kFakeLeakCommit.size()) == 0);

    // Unexpected commit bytes are refused without patching anything.
    lethal[kFakeLethalHitOffset + 9] = std::byte{0x08};
    REQUIRE_FALSE(hook.install(
        reinterpret_cast<void*>(&fake_game_screen_init),
        reinterpret_cast<void*>(&fake_game_screen_uninit), kFakeSpawnRejection,
        lethal + kFakeLethalHitOffset, leak + 6,
        [](void*, bool) { return false; }, [](void*) { return false; },
        [](void*) { return false; }, error));
    REQUIRE(error.find("lethal-hit") != std::string::npos);
    REQUIRE(std::memcmp(leak, kFakeLeakCommit.data(), kFakeLeakCommit.size()) == 0);
#pragma warning(suppress : 6001)  // Guarded above; Catch2 REQUIRE confuses code analysis.
    REQUIRE(VirtualFree(lethal, 0, MEM_RELEASE) != FALSE);
    REQUIRE(VirtualFree(leak, 0, MEM_RELEASE) != FALSE);
}

TEST_CASE("native event hook filters vtables and preserves dispatch ordering", "[hooks]") {
    std::vector<std::string> calls;
    FakeEventManager manager{&calls};
    int started_type = 1;
    int ended_type = 2;
    int unrelated_type = 3;
    int post_only_type = 4;
    btd5loader::runtime::NativeEventHook hook;
    std::string error;
    REQUIRE(hook.install(
        reinterpret_cast<void*>(&fake_event_dispatch),
        {
            {
                &started_type,
                {},
                [&calls](void*) { calls.emplace_back("starting"); },
                [&calls](void*) { calls.emplace_back("started"); },
            },
            {
                &ended_type,
                {},
                [&calls](void*) { calls.emplace_back("ending"); },
                [&calls](void*) { calls.emplace_back("ended"); },
            },
            {
                &post_only_type,
                [](void* event) { return static_cast<FakeNativeEvent*>(event)->vtable; },
                {},
                [&calls, &post_only_type](void* captured) {
                    REQUIRE(captured == &post_only_type);
                    calls.emplace_back("completed");
                },
            },
        },
        error));
    REQUIRE(error.empty());
    REQUIRE(hook.installed());

    FakeNativeEvent started_event{&started_type};
    REQUIRE(fake_event_dispatch(&manager, nullptr, &started_event, false));
    REQUIRE(calls == std::vector<std::string>{"starting", "original", "started"});

    calls.clear();
    FakeNativeEvent ended_event{&ended_type};
    REQUIRE(fake_event_dispatch(&manager, nullptr, &ended_event, true));
    REQUIRE(calls == std::vector<std::string>{"ending", "original_queued", "ended"});

    calls.clear();
    FakeNativeEvent post_only_event{&post_only_type};
    REQUIRE(fake_event_dispatch(&manager, nullptr, &post_only_event, false));
    REQUIRE(calls == std::vector<std::string>{"original", "completed"});

    calls.clear();
    FakeNativeEvent unrelated_event{&unrelated_type};
    REQUIRE(fake_event_dispatch(&manager, nullptr, &unrelated_event, false));
    REQUIRE(calls == std::vector<std::string>{"original"});

    hook.remove();
    REQUIRE_FALSE(hook.installed());
    calls.clear();
    started_event.vtable = &started_type;
    REQUIRE(fake_event_dispatch(&manager, nullptr, &started_event, false));
    REQUIRE(calls == std::vector<std::string>{"original"});
}

TEST_CASE("lives hook reports only verified gain and loss mutations", "[hooks]") {
    FakeLivesState state{{}, 100};
    FakeGainLivesObserver gain_observer{{}, &state};
    FakeLossLivesObserver loss_observer{{}, &state};
    std::vector<std::pair<std::int32_t, std::int32_t>> changes;
    btd5loader::runtime::LivesHook hook;
    std::string error;
    REQUIRE(hook.install(
        reinterpret_cast<void*>(&fake_gain_lives_handler),
        reinterpret_cast<void*>(&fake_loss_lives_handler),
        [&changes, &state](void* player, const std::int32_t before, const std::int32_t after) {
            REQUIRE(player == &state);
            changes.emplace_back(before, after);
        },
        error));
    REQUIRE(error.empty());
    REQUIRE(hook.installed());

    std::int32_t amount = 5;
    fake_gain_lives_handler(&gain_observer, nullptr, &amount, false);
    REQUIRE(state.lives == 105);
    REQUIRE(changes == std::vector<std::pair<std::int32_t, std::int32_t>>{{100, 105}});

    amount = 0;
    fake_gain_lives_handler(&gain_observer, nullptr, &amount, false);
    REQUIRE(changes.size() == 1);

    amount = 7;
    fake_loss_lives_handler(&loss_observer, nullptr, &amount, false);
    REQUIRE(state.lives == 98);
    REQUIRE(changes.back() == std::pair<std::int32_t, std::int32_t>{105, 98});

    loss_observer.state = nullptr;
    fake_loss_lives_handler(&loss_observer, nullptr, &amount, false);
    REQUIRE(changes.size() == 2);

    hook.remove();
    REQUIRE_FALSE(hook.installed());
    gain_observer.state = &state;
    fake_gain_lives_handler(&gain_observer, nullptr, &amount, false);
    REQUIRE(state.lives == 105);
    REQUIRE(changes.size() == 2);
}

TEST_CASE("lives write hook fires immediately before committed writes", "[hooks]") {
    FakeLivesState state{{}, 100};
    std::vector<std::pair<std::int32_t, std::int32_t>> changes;
    btd5loader::runtime::LivesWriteHook hook;
    std::string error;
    void* const gain_target =
        resolve_fake_lives_write_target(reinterpret_cast<void*>(&fake_gain_lives_write));
    void* const loss_target =
        resolve_fake_lives_write_target(reinterpret_cast<void*>(&fake_loss_lives_write));
    REQUIRE(gain_target != loss_target);
    const bool installed = hook.install(
        gain_target,
        loss_target,
        [&state, &changes](void* player, const std::int32_t before, const std::int32_t after) {
            REQUIRE(player == &state);
            REQUIRE(state.lives == before);
            changes.emplace_back(before, after);
            return btd5loader::runtime::LivesChangeDecision{false, after};
        },
        error);
    INFO("Lives write hook installation error: " << error);
    REQUIRE(installed);
    REQUIRE(error.empty());
    REQUIRE(hook.installed());

    invoke_fake_lives_write(&state, 5, gain_target);
    REQUIRE(state.lives == 105);
    REQUIRE(changes == std::vector<std::pair<std::int32_t, std::int32_t>>{{100, 105}});

    invoke_fake_lives_write(&state, 7, loss_target);
    REQUIRE(state.lives == 98);
    REQUIRE(changes.back() == std::pair<std::int32_t, std::int32_t>{105, 98});

    state.lives = 3;
    invoke_fake_lives_write(&state, 7, loss_target);
    REQUIRE(state.lives == -4);
    REQUIRE(changes.back() == std::pair<std::int32_t, std::int32_t>{3, 0});

    // Writes that leave the stored value unchanged are not lives changes: a
    // loss clamped at zero and a zero-point gain run natively without dispatch.
    const auto dispatched = changes.size();
    state.lives = 0;
    invoke_fake_lives_write(&state, 7, loss_target);
    REQUIRE(state.lives == -7);
    state.lives = 40;
    invoke_fake_lives_write(&state, 0, gain_target);
    REQUIRE(state.lives == 40);
    invoke_fake_lives_write(&state, 0, loss_target);
    REQUIRE(state.lives == 40);
    REQUIRE(changes.size() == dispatched);

    bool cancel_next = true;
    hook.remove();
    REQUIRE(hook.install(
        gain_target,
        loss_target,
        [&cancel_next, &changes, &state](void* player, const std::int32_t before, const std::int32_t after) {
            REQUIRE(player == &state);
            changes.emplace_back(before, after);
            const bool cancelled = std::exchange(cancel_next, false);
            return btd5loader::runtime::LivesChangeDecision{
                cancelled, cancelled ? after + 10 : after};
        },
        error));
    state.lives = 50;
    invoke_fake_lives_write(&state, 5, gain_target);
    REQUIRE(state.lives == 50);
    REQUIRE(changes.back() == std::pair<std::int32_t, std::int32_t>{50, 55});
    cancel_next = true;
    invoke_fake_lives_write(&state, 7, loss_target);
    REQUIRE(state.lives == 50);
    REQUIRE(changes.back() == std::pair<std::int32_t, std::int32_t>{50, 43});
    invoke_fake_lives_write(&state, 5, gain_target);
    REQUIRE(state.lives == 55);

    hook.remove();
    REQUIRE(hook.install(
        gain_target,
        loss_target,
        [&state](void* player, const std::int32_t, const std::int32_t after) {
            REQUIRE(player == &state);
            return btd5loader::runtime::LivesChangeDecision{false, after + 10};
        },
        error));
    state.lives = 100;
    invoke_fake_lives_write(&state, 5, gain_target);
    REQUIRE(state.lives == 115);
    invoke_fake_lives_write(&state, 20, loss_target);
    REQUIRE(state.lives == 105);

    hook.remove();
    REQUIRE_FALSE(hook.installed());
    changes.clear();
    invoke_fake_lives_write(&state, 4, gain_target);
    REQUIRE(state.lives == 109);
    REQUIRE(changes.empty());
}

TEST_CASE("an unknown executable and asset pair fails closed", "[compatibility]") {
    const auto test_root = std::filesystem::temp_directory_path() /
                           (L"btd5ml-detect-test-" + std::to_wstring(GetCurrentProcessId()));
    const auto game_root = test_root / L"game";
    std::filesystem::create_directories(game_root / L"Assets");
    {
        std::ofstream executable(game_root / L"BTD5-Win.exe", std::ios::binary);
        executable << "not-a-supported-executable";
        std::ofstream assets(game_root / L"Assets" / L"BTD5.jet", std::ios::binary);
        assets << "not-a-supported-asset-archive";
    }
    const auto maps = std::filesystem::path(BTD5ML_SOURCE_DIR) / L"symbols";
    const auto detection = btd5loader::runtime::detect_build(game_root, maps);
    REQUIRE_FALSE(detection.build.has_value());
    REQUIRE(detection.error == "unsupported game build");
    REQUIRE_FALSE(detection.fingerprints.executable_sha256.empty());
    REQUIRE_FALSE(detection.fingerprints.assets_sha256.empty());
    std::filesystem::remove_all(test_root);
}

TEST_CASE("a required resolver failure identifies its stable symbol name", "[symbols]") {
    std::vector<wchar_t> executable_path(32768);
    const DWORD length = GetModuleFileNameW(
        nullptr, executable_path.data(), static_cast<DWORD>(executable_path.size()));
    REQUIRE(length > 0);
    REQUIRE(length < executable_path.size());

    btd5loader::runtime::BuildDefinition definition;
    btd5loader::runtime::SymbolDefinition symbol;
    symbol.name = "game.injected.failure";
    symbol.module = "BTD5-Win.exe";
    symbol.section = ".section-that-does-not-exist";
    symbol.pattern = "55 8B EC";
    symbol.required = true;
    definition.symbols.push_back(std::move(symbol));

    const auto report = btd5loader::runtime::resolve_symbols_from_image(
        definition, std::filesystem::path(executable_path.data(), executable_path.data() + length));
    REQUIRE_FALSE(report.success);
    REQUIRE(report.diagnostics.size() == 1);
    REQUIRE(report.diagnostics.front().name == "game.injected.failure");
    REQUIRE(report.diagnostics.front().required);
    REQUIRE(report.diagnostics.front().message == "section not found");
}

TEST_CASE("isolated Lua mod receives lifecycle APIs and persistent storage", "[lua]") {
    const auto test_root = std::filesystem::temp_directory_path() /
                           (L"btd5ml-lua-test-" + std::to_wstring(GetCurrentProcessId()));
    const auto resources = test_root / L"resources";
    std::filesystem::create_directories(resources);
    {
        std::ofstream resource(resources / L"message.txt", std::ios::binary);
        resource << "packaged resource";
    }

    std::vector<std::string> logs;
    btd5loader::runtime::LuaModOptions options;
    options.mod_id = "sample.lifecycle";
    options.storage_directory = test_root / L"storage" / L"sample.lifecycle";
    options.resource_directory = resources;
    options.configuration.emplace("greeting", "hello");
    options.localization.emplace("sample.ready", "Ready!");
    options.log = [&logs](const std::string_view level, const std::string_view message) {
        logs.emplace_back(std::string(level) + ":" + std::string(message));
    };

    {
        btd5loader::runtime::LuaMod mod(std::move(options));
        REQUIRE(mod.valid());
        const std::string script = R"lua(
            assert(io == nil and os == nil and package == nil and debug == nil)
            assert(require == nil and loadfile == nil and dofile == nil and load == nil)
            function on_load()
                assert(btd5.config.get("greeting") == "hello")
                assert(btd5.localization.get("sample.ready") == "Ready!")
                assert(btd5.resource.read_text("message.txt") == "packaged resource")
                assert(not pcall(btd5.resource.read_text, "../outside.txt"))
                btd5.storage.set("launches", "1")
                btd5.timer.after(2, function() btd5.log("info", "timer") end)
                btd5.log("info", "load")
            end
            function on_ready() btd5.log("info", "ready") end
            function on_shutdown() btd5.log("info", "shutdown") end
        )lua";
        REQUIRE(mod.load_script(script, "main.lua"));
        REQUIRE(mod.invoke("on_load"));
        REQUIRE(mod.invoke("on_ready"));
        mod.advance_timers(1);
        REQUIRE(std::find(logs.begin(), logs.end(), "info:timer") == logs.end());
        mod.advance_timers(1);
        REQUIRE(mod.invoke("on_shutdown"));
        REQUIRE(mod.memory_used() <= 16U * 1024U * 1024U);
    }

    REQUIRE(std::find(logs.begin(), logs.end(), "info:load") != logs.end());
    REQUIRE(std::find(logs.begin(), logs.end(), "info:ready") != logs.end());
    REQUIRE(std::find(logs.begin(), logs.end(), "info:timer") != logs.end());
    REQUIRE(std::find(logs.begin(), logs.end(), "info:shutdown") != logs.end());

    btd5loader::runtime::LuaModOptions reload_options;
    reload_options.mod_id = "sample.lifecycle";
    reload_options.storage_directory = test_root / L"storage" / L"sample.lifecycle";
    reload_options.resource_directory = resources;
    btd5loader::runtime::LuaMod reloaded(std::move(reload_options));
    REQUIRE(reloaded.load_script(
        "function on_load() assert(btd5.storage.get('launches') == '1') end",
        "reload.lua"));
    REQUIRE(reloaded.invoke("on_load"));

    btd5loader::runtime::LuaModOptions other_options;
    other_options.mod_id = "sample.other";
    other_options.storage_directory = test_root / L"storage" / L"sample.other";
    other_options.resource_directory = resources;
    btd5loader::runtime::LuaMod other(std::move(other_options));
    REQUIRE(other.load_script(
        "function on_load() assert(btd5.storage.get('launches') == nil) end",
        "main.lua"));
    REQUIRE(other.invoke("on_load"));
    std::filesystem::remove_all(test_root);
}

TEST_CASE("runaway Lua callback is disabled without affecting another mod", "[lua]") {
    btd5loader::runtime::LuaModOptions runaway_options;
    runaway_options.mod_id = "sample.runaway";
    runaway_options.instruction_budget = 5'000;
    btd5loader::runtime::LuaMod runaway(std::move(runaway_options));
    REQUIRE(runaway.load_script("function on_ready() while true do end end", "main.lua"));
    REQUIRE_FALSE(runaway.invoke("on_ready"));
    REQUIRE(runaway.callback_disabled("on_ready"));
    REQUIRE(runaway.last_error().find("sample.runaway.on_ready") != std::string_view::npos);
    REQUIRE(runaway.last_error().find("instruction budget") != std::string_view::npos);

    btd5loader::runtime::LuaModOptions healthy_options;
    healthy_options.mod_id = "sample.healthy";
    btd5loader::runtime::LuaMod healthy(std::move(healthy_options));
    REQUIRE(healthy.load_script("function on_ready() return 42 end", "main.lua"));
    REQUIRE(healthy.invoke("on_ready"));
    REQUIRE_FALSE(healthy.callback_disabled("on_ready"));
}

TEST_CASE("Lua memory and recursion limits contain abusive callbacks", "[lua]") {
    btd5loader::runtime::LuaModOptions memory_options;
    memory_options.mod_id = "sample.memory";
    memory_options.memory_limit_bytes = 512U * 1024U;
    btd5loader::runtime::LuaMod memory_limited(std::move(memory_options));
    REQUIRE(memory_limited.valid());
    REQUIRE(memory_limited.load_script(
        "function on_ready() local value = string.rep('x', 2 * 1024 * 1024) end",
        "main.lua"));
    REQUIRE_FALSE(memory_limited.invoke("on_ready"));
    REQUIRE(memory_limited.callback_disabled("on_ready"));

    btd5loader::runtime::LuaModOptions recursion_options;
    recursion_options.mod_id = "sample.recursion";
    recursion_options.instruction_budget = 1'000'000;
    btd5loader::runtime::LuaMod recursion_limited(std::move(recursion_options));
    REQUIRE(recursion_limited.load_script(
        "function recurse(n) return n + recurse(n + 1) end "
        "function on_ready() recurse(1) end",
        "main.lua"));
    REQUIRE_FALSE(recursion_limited.invoke("on_ready"));
    REQUIRE(recursion_limited.callback_disabled("on_ready"));
    REQUIRE(recursion_limited.last_error().find("recursion limit") != std::string_view::npos);
}

TEST_CASE("sample mod manifest satisfies the v1 contract", "[packages]") {
    const auto manifest_path = std::filesystem::path(BTD5ML_SOURCE_DIR) /
                               L"samples" / L"lifecycle-mod" / L"mod.json";
    std::ifstream input(manifest_path, std::ios::binary);
    REQUIRE(input.good());
    const std::string contents{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    std::string error;
    const auto manifest = btd5loader::runtime::parse_mod_manifest(contents, error);
    REQUIRE(manifest.has_value());
    REQUIRE(error.empty());
    REQUIRE(manifest->id == "sample.lifecycle");
    REQUIRE(manifest->entry_point == "lua/main.lua");
    REQUIRE(manifest->loader_api == 1);
}

TEST_CASE("published Lua example manifests and entry points load", "[packages][lua]") {
    struct Example final {
        const wchar_t* directory;
        const char* id;
    };
    constexpr std::array examples{
        Example{L"hello-world-mod", "example.hello-world"},
        Example{L"event-monitor-mod", "example.event-monitor"},
        Example{L"lives-guardian-mod", "example.lives-guardian"},
        Example{L"api-debug-mod", "example.api-debug"},
    };
    const auto samples = std::filesystem::path(BTD5ML_SOURCE_DIR) / L"samples";
    const auto test_root = std::filesystem::temp_directory_path() /
                           (L"btd5ml-example-mods-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(test_root);

    for (const auto& example : examples) {
        const auto source = samples / example.directory;
        std::string error;
        const auto manifest = btd5loader::runtime::parse_mod_manifest(
            read_test_file(source / L"mod.json"), error);
        REQUIRE(manifest.has_value());
        REQUIRE(error.empty());
        REQUIRE(manifest->id == example.id);

        btd5loader::runtime::LuaModOptions options;
        options.mod_id = manifest->id;
        options.storage_directory = test_root / std::filesystem::path(manifest->id);
        options.resource_directory = source;
        options.configuration.emplace("enabled", "true");
        options.configuration.emplace("minimum_lives", "1");
        options.configuration.emplace("log_each_action", "false");
        btd5loader::runtime::LuaMod mod(std::move(options));
        REQUIRE(mod.load_script(
            read_test_file(source / std::filesystem::path(manifest->entry_point)),
            manifest->entry_point));
        REQUIRE(mod.invoke("on_load"));
        REQUIRE(mod.invoke("on_ready"));
        mod.advance_timers(60);
        REQUIRE(mod.invoke("on_shutdown"));
    }

    std::filesystem::remove_all(test_root);
}

TEST_CASE("API Debug sample exercises opt-in mutations and cancellations", "[samples][lua]") {
    using btd5loader::runtime::GameObjectKind;
    struct TowerState final {
        std::int32_t pop_count{4};
        std::int32_t sell_price{100};
    } tower;
    struct BloonState final {
        float health{2.0F};
    } bloon;

    const auto source = std::filesystem::path(BTD5ML_SOURCE_DIR) /
                        L"samples" / L"api-debug-mod";
    const auto test_root = std::filesystem::temp_directory_path() /
                           (L"btd5ml-api-debug-" + std::to_wstring(GetCurrentProcessId()));
    btd5loader::runtime::GameObjectRegistry registry;
    const auto tower_handle = registry.add(GameObjectKind::Tower, &tower);
    const auto bloon_handle = registry.add(GameObjectKind::Bloon, &bloon);
    std::vector<std::string> logs;

    btd5loader::runtime::LuaModOptions options;
    options.mod_id = "example.api-debug";
    options.storage_directory = test_root;
    options.resource_directory = source;
    options.localization.emplace("example.api-debug.ready", "ready");
    options.configuration = {
        {"log_event_details", "true"},
        {"detailed_event_limit", "2"},
        {"log_round_summaries", "false"},
        {"log_object_properties", "true"},
        {"verify_terminal_wrappers", "false"},
        {"operation_limit", "1"},
        {"mutate_tower_pop_count", "true"},
        {"tower_pop_count", "321"},
        {"mutate_tower_sell_price", "true"},
        {"tower_sell_price", "654"},
        {"mutate_bloon_health", "true"},
        {"bloon_health_bonus", "2.5"},
        {"replace_lives_below_minimum", "true"},
        {"minimum_lives", "5"},
        {"cancel_lives_losses", "false"},
        {"cancel_tower_upgrades", "true"},
        {"cancel_tower_sales", "true"},
        {"cancel_bloon_leaks", "true"}};
    options.object_registry = &registry;
    options.log = [&logs](const std::string_view, const std::string_view message) {
        logs.emplace_back(message);
    };
    options.game_object_integer_get = [](
        const GameObjectKind kind,
        void* const object,
        const std::string_view property) -> std::optional<std::int64_t> {
        if (kind != GameObjectKind::Tower || object == nullptr) return std::nullopt;
        const auto* state = static_cast<TowerState*>(object);
        if (property == "pop_count") return state->pop_count;
        if (property == "sell_price") return state->sell_price;
        return std::nullopt;
    };
    options.game_object_integer_set = [](
        const GameObjectKind kind,
        void* const object,
        const std::string_view property,
        const std::int64_t value,
        std::string&) {
        if (kind != GameObjectKind::Tower || object == nullptr) return false;
        auto* state = static_cast<TowerState*>(object);
        if (property == "pop_count") state->pop_count = static_cast<std::int32_t>(value);
        else if (property == "sell_price") state->sell_price = static_cast<std::int32_t>(value);
        else return false;
        return true;
    };
    options.game_object_number_get = [](
        const GameObjectKind kind,
        void* const object,
        const std::string_view property) -> std::optional<double> {
        if (kind != GameObjectKind::Bloon || object == nullptr || property != "health") {
            return std::nullopt;
        }
        return static_cast<BloonState*>(object)->health;
    };
    options.game_object_number_set = [](
        const GameObjectKind kind,
        void* const object,
        const std::string_view property,
        const double value,
        std::string&) {
        if (kind != GameObjectKind::Bloon || object == nullptr || property != "health") {
            return false;
        }
        static_cast<BloonState*>(object)->health = static_cast<float>(value);
        return true;
    };

    btd5loader::runtime::LuaMod mod(std::move(options));
    REQUIRE(mod.load_script(read_test_file(source / L"lua" / L"main.lua"), "lua/main.lua"));
    const bool loaded = mod.invoke("on_load");
    INFO("API Debug on_load error: " << mod.last_error());
    REQUIRE(loaded);
    REQUIRE(mod.invoke("on_ready"));

    REQUIRE(mod.dispatch_event("tower.placed", {{"tower", tower_handle}}).succeeded);
    REQUIRE(tower.pop_count == 321);
    REQUIRE(tower.sell_price == 654);
    REQUIRE(mod.dispatch_event("bloon.spawned", {{"bloon", bloon_handle}}).succeeded);
    REQUIRE(bloon.health == 4.5F);
    tower.pop_count = 7;
    tower.sell_price = 200;
    REQUIRE(mod.dispatch_event("tower.placed", {{"tower", tower_handle}}).succeeded);
    REQUIRE(tower.pop_count == 7);
    REQUIRE(tower.sell_price == 654);
    bloon.health = 3.0F;
    for (int attempt = 0; attempt < 4; ++attempt) {
        REQUIRE(mod.dispatch_event("bloon.spawned", {{"bloon", bloon_handle}}).succeeded);
    }
    REQUIRE(bloon.health == 3.0F);
    REQUIRE(std::count_if(logs.begin(), logs.end(), [](const std::string& message) {
        return message.find("event #") != std::string::npos &&
               message.find("bloon.spawned") != std::string::npos;
    }) == 2);

    const auto lives = mod.dispatch_event(
        "lives.changing",
        {{"old_lives", std::int64_t{10}}, {"new_lives", std::int64_t{1}}},
        true);
    REQUIRE(lives.succeeded);
    REQUIRE_FALSE(lives.cancelled);
    REQUIRE(std::get<std::int64_t>(lives.fields.at(1).second) == 5);
    const auto second_lives = mod.dispatch_event(
        "lives.changing",
        {{"old_lives", std::int64_t{10}}, {"new_lives", std::int64_t{1}}},
        true);
    REQUIRE(std::get<std::int64_t>(second_lives.fields.at(1).second) == 1);
    REQUIRE(mod.dispatch_event("tower.upgrading", {{"tower", tower_handle}}, true).cancelled);
    REQUIRE_FALSE(mod.dispatch_event("tower.upgrading", {{"tower", tower_handle}}, true).cancelled);
    REQUIRE(mod.dispatch_event("tower.selling", {{"tower", tower_handle}}, true).cancelled);
    REQUIRE_FALSE(mod.dispatch_event("tower.selling", {{"tower", tower_handle}}, true).cancelled);
    REQUIRE(mod.dispatch_event("bloon.leaking", {{"bloon", bloon_handle}}, true).cancelled);
    REQUIRE_FALSE(mod.dispatch_event("bloon.leaking", {{"bloon", bloon_handle}}, true).cancelled);

    mod.advance_timers(60);
    REQUIRE(mod.invoke("on_shutdown"));
    std::filesystem::remove_all(test_root);
}

TEST_CASE("Lives Guardian example cancels only losses below its floor", "[samples][lua]") {
    const auto source = std::filesystem::path(BTD5ML_SOURCE_DIR) /
                        L"samples" / L"lives-guardian-mod";
    btd5loader::runtime::LuaModOptions options;
    options.mod_id = "example.lives-guardian";
    options.configuration.emplace("enabled", "true");
    options.configuration.emplace("minimum_lives", "1");
    btd5loader::runtime::LuaMod mod(std::move(options));
    REQUIRE(mod.load_script(read_test_file(source / L"lua" / L"main.lua"), "lua/main.lua"));
    REQUIRE(mod.invoke("on_load"));

    const auto safe_loss = mod.dispatch_event(
        "lives.changing",
        {{"old_lives", std::int64_t{2}}, {"new_lives", std::int64_t{1}}},
        true);
    REQUIRE(safe_loss.succeeded);
    REQUIRE_FALSE(safe_loss.cancelled);

    const auto protected_loss = mod.dispatch_event(
        "lives.changing",
        {{"old_lives", std::int64_t{1}}, {"new_lives", std::int64_t{0}}},
        true);
    REQUIRE(protected_loss.succeeded);
    REQUIRE(protected_loss.cancelled);

    const auto gain = mod.dispatch_event(
        "lives.changing",
        {{"old_lives", std::int64_t{1}}, {"new_lives", std::int64_t{2}}},
        true);
    REQUIRE(gain.succeeded);
    REQUIRE_FALSE(gain.cancelled);
}

TEST_CASE("manifest rejects traversal and unsupported API versions", "[packages]") {
    const std::string invalid = R"json({
        "id":"sample.invalid","name":"Invalid","author":"Tester","version":"1.0.0",
        "entry_point":"../outside.lua","loader_api":99,
        "supported_game_builds":["steam-win32-4.8"],"dependencies":[],
        "load_order":{"before":[],"after":[]},"capabilities":[]
    })json";
    std::string error;
    REQUIRE_FALSE(btd5loader::runtime::parse_mod_manifest(invalid, error).has_value());
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("load order honors dependencies then profile order and mod ID", "[packages]") {
    const auto version = *btd5loader::runtime::parse_semantic_version("1.0.0");
    btd5loader::runtime::ModManifest base;
    base.id = "sample.base";
    base.version = version;
    btd5loader::runtime::ModManifest dependent;
    dependent.id = "sample.dependent";
    dependent.version = version;
    dependent.dependencies.push_back({"sample.base", "^1.0.0"});
    btd5loader::runtime::ModManifest alpha;
    alpha.id = "sample.alpha";
    alpha.version = version;

    const auto result = btd5loader::runtime::resolve_load_order(
        {dependent, base, alpha}, {{"sample.base", 0}, {"sample.dependent", 1}});
    REQUIRE(result.error.empty());
    REQUIRE(result.ordered_ids ==
            std::vector<std::string>{"sample.base", "sample.dependent", "sample.alpha"});
}

TEST_CASE("duplicate IDs and dependency cycles are rejected", "[packages]") {
    const auto version = *btd5loader::runtime::parse_semantic_version("1.0.0");
    btd5loader::runtime::ModManifest first;
    first.id = "sample.first";
    first.version = version;
    btd5loader::runtime::ModManifest second;
    second.id = "sample.second";
    second.version = version;
    first.dependencies.push_back({second.id, "*"});
    second.dependencies.push_back({first.id, "*"});

    const auto cycle = btd5loader::runtime::resolve_load_order({first, second}, {});
    REQUIRE(cycle.ordered_ids.empty());
    REQUIRE(cycle.error.find("cycle") != std::string::npos);

    second.id = first.id;
    second.dependencies.clear();
    const auto duplicate = btd5loader::runtime::resolve_load_order({first, second}, {});
    REQUIRE(duplicate.ordered_ids.empty());
    REQUIRE(duplicate.error.find("duplicate") != std::string::npos);
}

TEST_CASE("valid btd5mod ZIP package is accepted", "[packages]") {
    const auto test_root = std::filesystem::temp_directory_path() /
                           (L"btd5ml-package-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(test_root);
    const auto package_path = test_root / L"sample.btd5mod";
    const std::string manifest = R"json({
        "id":"sample.archive","name":"Archive","author":"Tester","version":"1.0.0",
        "entry_point":"lua/main.lua","loader_api":1,
        "supported_game_builds":["steam-win32-4.8"],"dependencies":[],
        "load_order":{"before":[],"after":[]},"capabilities":["storage"]
    })json";
    REQUIRE(create_test_zip(
        package_path,
        {{"mod.json", manifest}, {"lua/main.lua", "function on_load() end"}}));

    std::string error;
    const auto package = btd5loader::runtime::validate_mod_package(package_path, error);
    REQUIRE(package.has_value());
    REQUIRE(error.empty());
    REQUIRE(package->manifest.id == "sample.archive");
    REQUIRE(package->files.size() == 2);
    std::filesystem::remove_all(test_root);
}

TEST_CASE("btd5mod package rejects traversal and missing entry points", "[packages]") {
    const auto test_root = std::filesystem::temp_directory_path() /
                           (L"btd5ml-package-invalid-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(test_root);
    const std::string manifest = R"json({
        "id":"sample.invalid","name":"Invalid","author":"Tester","version":"1.0.0",
        "entry_point":"lua/missing.lua","loader_api":1,
        "supported_game_builds":["steam-win32-4.8"],"dependencies":[],
        "load_order":{"before":[],"after":[]},"capabilities":[]
    })json";

    const auto traversal_path = test_root / L"traversal.btd5mod";
    REQUIRE(create_test_zip(
        traversal_path,
        {{"mod.json", manifest}, {"../escape.txt", "blocked"}}));
    std::string error;
    REQUIRE_FALSE(btd5loader::runtime::validate_mod_package(traversal_path, error).has_value());
    REQUIRE(error.find("unsafe") != std::string::npos);

    const auto missing_path = test_root / L"missing.btd5mod";
    REQUIRE(create_test_zip(missing_path, {{"mod.json", manifest}}));
    error.clear();
    REQUIRE_FALSE(btd5loader::runtime::validate_mod_package(missing_path, error).has_value());
    REQUIRE(error.find("entry point") != std::string::npos);

    const auto malformed_path = test_root / L"malformed.btd5mod";
    {
        std::ofstream malformed(malformed_path, std::ios::binary);
        malformed << "this is not a ZIP archive";
    }
    error.clear();
    REQUIRE_FALSE(btd5loader::runtime::validate_mod_package(malformed_path, error).has_value());
    REQUIRE(error.find("valid ZIP") != std::string::npos);

    const auto limited_path = test_root / L"limited.btd5mod";
    REQUIRE(create_test_zip(
        limited_path,
        {{"mod.json", manifest}, {"lua/missing.lua", "function on_load() end"}}));
    btd5loader::runtime::ModPackageLimits limits;
    limits.maximum_file_uncompressed_bytes = 8;
    error.clear();
    REQUIRE_FALSE(btd5loader::runtime::validate_mod_package(limited_path, error, limits).has_value());
    REQUIRE(error.find("size limit") != std::string::npos);
    std::filesystem::remove_all(test_root);
}

TEST_CASE("lifecycle sample observes attack and projectile lifetimes when enabled", "[samples][lua]") {
    using btd5loader::runtime::GameObjectKind;
    const auto source = std::filesystem::path(BTD5ML_SOURCE_DIR) /
                        L"samples" / L"lifecycle-mod";
    btd5loader::runtime::GameObjectRegistry registry;
    std::vector<std::string> logs;
    btd5loader::runtime::LuaModOptions options;
    options.mod_id = "sample.lifecycle";
    options.storage_directory = std::filesystem::temp_directory_path() /
                                (L"btd5ml-lifetime-sample-" + std::to_wstring(GetCurrentProcessId()));
    options.resource_directory = source;
    options.configuration = {
        {"greeting", "Hello from Lua"},
        {"observe_attacks_projectiles", "true"}};
    options.object_registry = &registry;
    options.log = [&logs](const std::string_view, const std::string_view message) {
        logs.emplace_back(message);
    };
    btd5loader::runtime::LuaMod mod(std::move(options));
    REQUIRE(mod.load_script(read_test_file(source / L"lua" / L"main.lua"), "lua/main.lua"));
    REQUIRE(mod.has_event_handlers("attack.created"));
    REQUIRE(mod.has_event_handlers("projectile.destroyed"));

    int attack = 0;
    std::array<int, 5> projectiles{};
    const auto attack_handle = registry.add(GameObjectKind::Attack, &attack);
    REQUIRE(mod.dispatch_event("attack.created", {{"attack", attack_handle}}).succeeded);
    for (auto& projectile : projectiles) {
        const auto handle = registry.add(GameObjectKind::Projectile, &projectile);
        REQUIRE(mod.dispatch_event("projectile.created", {{"projectile", handle}}).succeeded);
        REQUIRE(mod.dispatch_event("projectile.destroyed", {{"projectile", handle}}).succeeded);
        REQUIRE(registry.invalidate(handle));
    }
    REQUIRE(mod.dispatch_event("attack.destroyed", {{"attack", attack_handle}}).succeeded);
    REQUIRE(registry.invalidate(attack_handle));
    mod.advance_timers(1);
    REQUIRE(mod.last_error().empty());

    const auto count = [&logs](const std::string_view prefix) {
        return std::count_if(logs.begin(), logs.end(), [prefix](const std::string& line) {
            return line.rfind(prefix, 0) == 0;
        });
    };
    REQUIRE(count("Lifecycle Sample observed projectile.created id=") == 3);
    REQUIRE(count("Lifecycle Sample observed projectile.destroyed id=") == 3);
    REQUIRE(count("Lifecycle Sample observed attack.created id=") == 1);
    REQUIRE(count("Lifecycle Sample observed attack.destroyed id=") == 1);
    REQUIRE(count("Lifecycle Sample confirmed destroyed projectile became stale") == 1);
    REQUIRE(count("Lifecycle Sample confirmed destroyed attack became stale") == 1);

    btd5loader::runtime::LuaModOptions disabled;
    disabled.mod_id = "sample.lifecycle";
    disabled.storage_directory = std::filesystem::temp_directory_path() /
                                 (L"btd5ml-lifetime-sample-off-" + std::to_wstring(GetCurrentProcessId()));
    disabled.resource_directory = source;
    disabled.configuration = {{"greeting", "Hello from Lua"}};
    btd5loader::runtime::LuaMod quiet(std::move(disabled));
    REQUIRE(quiet.load_script(read_test_file(source / L"lua" / L"main.lua"), "lua/main.lua"));
    REQUIRE_FALSE(quiet.has_event_handlers("projectile.created"));
    REQUIRE_FALSE(quiet.has_event_handlers("attack.destroyed"));
}

TEST_CASE("packaged lifecycle sample extracts and runs through shutdown", "[packages][lua]") {
    const auto source = std::filesystem::path(BTD5ML_SOURCE_DIR) /
                        L"samples" / L"lifecycle-mod";
    const auto test_root = std::filesystem::temp_directory_path() /
                           (L"btd5ml-sample-package-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(test_root);
    const auto archive = test_root / L"lifecycle.btd5mod";
    REQUIRE(create_test_zip(
        archive,
        {
            {"mod.json", read_test_file(source / L"mod.json")},
            {"lua/main.lua", read_test_file(source / L"lua" / L"main.lua")},
            {"localization/en-US.json", read_test_file(source / L"localization" / L"en-US.json")},
            {"README.md", read_test_file(source / L"README.md")},
        }));

    std::string error;
    const auto package = btd5loader::runtime::validate_mod_package(archive, error);
    REQUIRE(package.has_value());
    const auto extracted = test_root / L"extracted";
    REQUIRE(btd5loader::runtime::extract_mod_package(*package, extracted, error));

    std::vector<std::string> logs;
    btd5loader::runtime::LuaModOptions options;
    options.mod_id = package->manifest.id;
    options.storage_directory = test_root / L"storage" / std::filesystem::path(package->manifest.id);
    options.resource_directory = extracted;
    options.configuration.emplace("greeting", "Hello from Lua");
    options.localization.emplace("sample.lifecycle.ready", "Lifecycle Sample is ready");
    options.log = [&logs](const std::string_view, const std::string_view message) {
        logs.emplace_back(message);
    };
    btd5loader::runtime::LuaMod mod(std::move(options));
    REQUIRE(mod.load_script(
        read_test_file(extracted / std::filesystem::path(package->manifest.entry_point)),
        package->manifest.entry_point));
    REQUIRE(mod.invoke("on_load"));
    REQUIRE(mod.invoke("on_ready"));
    mod.advance_timers(60);
    REQUIRE(mod.invoke("on_shutdown"));
    REQUIRE(logs.size() == 4);
    REQUIRE(logs.back() == "Lifecycle Sample shut down cleanly");
    std::filesystem::remove_all(test_root);
}
