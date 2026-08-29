#include <btd5loader/version.hpp>
#include <btd5loader/runtime_api.hpp>
#include <catch2/catch_test_macros.hpp>
#include <miniz.h>
#include <nlohmann/json.hpp>

#include <Windows.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "../../src/native/runtime/compatibility.hpp"
#include "../../src/native/runtime/active_profile.hpp"
#include "../../src/native/runtime/hook_transaction.hpp"
#include "../../src/native/runtime/lives_hook.hpp"
#include "../../src/native/runtime/lives_write_hook.hpp"
#include "../../src/native/runtime/lua_mod.hpp"
#include "../../src/native/runtime/match_hook.hpp"
#include "../../src/native/runtime/mod_manifest.hpp"
#include "../../src/native/runtime/mod_package.hpp"
#include "../../src/native/runtime/pattern.hpp"
#include "../../src/native/runtime/native_event_hook.hpp"
#include "../../src/native/runtime/runtime_state.hpp"
#include "../../src/native/runtime/symbol_resolver.hpp"
#include "../../src/native/runtime/tower_placement_hook.hpp"

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
    REQUIRE(logs == std::vector<std::string>{"first:4", "second:5"});

    logs.clear();
    const auto second = mod.dispatch_event("match.starting", fields, false);
    REQUIRE(second.succeeded);
    REQUIRE_FALSE(second.cancelled);
    REQUIRE(second.handlers_invoked == 3);
    REQUIRE(logs == std::vector<std::string>{"first:4", "second:5", "late:5"});
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
            "bloon.popping", "bloon.popped", "bloon.leaking", "bloon.leaked"
        }
        for _, name in ipairs(names) do
            btd5.events.on(name, function() end)
        end
    )lua", "event-catalog.lua"));
    REQUIRE(mod.dispatch_event("match.starting").handlers_invoked == 1);
    REQUIRE(mod.dispatch_event("round.ended").handlers_invoked == 1);
    REQUIRE(mod.dispatch_event("cash.changing").handlers_invoked == 1);
    REQUIRE(mod.dispatch_event("lives.changed").handlers_invoked == 1);
    REQUIRE(mod.dispatch_event("tower.upgraded").handlers_invoked == 1);
    REQUIRE(mod.dispatch_event("bloon.leaked").handlers_invoked == 1);
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
        [&calls]() { calls.emplace_back("starting"); },
        [&calls]() { calls.emplace_back("started"); },
        [&calls]() { calls.emplace_back("ending"); },
        [&calls]() { calls.emplace_back("ended"); },
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
        [&changes](const std::int32_t before, const std::int32_t after) {
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
    REQUIRE(hook.install(
        reinterpret_cast<void*>(&fake_gain_lives_write),
        reinterpret_cast<void*>(&fake_loss_lives_write),
        [&state, &changes](const std::int32_t before, const std::int32_t after) {
            REQUIRE(state.lives == before);
            changes.emplace_back(before, after);
            return false;
        },
        error));
    REQUIRE(error.empty());
    REQUIRE(hook.installed());

    invoke_fake_lives_write(&state, 5, reinterpret_cast<void*>(&fake_gain_lives_write));
    REQUIRE(state.lives == 105);
    REQUIRE(changes == std::vector<std::pair<std::int32_t, std::int32_t>>{{100, 105}});

    invoke_fake_lives_write(&state, 7, reinterpret_cast<void*>(&fake_loss_lives_write));
    REQUIRE(state.lives == 98);
    REQUIRE(changes.back() == std::pair<std::int32_t, std::int32_t>{105, 98});

    state.lives = 3;
    invoke_fake_lives_write(&state, 7, reinterpret_cast<void*>(&fake_loss_lives_write));
    REQUIRE(state.lives == -4);
    REQUIRE(changes.back() == std::pair<std::int32_t, std::int32_t>{3, 0});

    bool cancel_next = true;
    hook.remove();
    REQUIRE(hook.install(
        reinterpret_cast<void*>(&fake_gain_lives_write),
        reinterpret_cast<void*>(&fake_loss_lives_write),
        [&cancel_next, &changes](const std::int32_t before, const std::int32_t after) {
            changes.emplace_back(before, after);
            return std::exchange(cancel_next, false);
        },
        error));
    state.lives = 50;
    invoke_fake_lives_write(&state, 5, reinterpret_cast<void*>(&fake_gain_lives_write));
    REQUIRE(state.lives == 50);
    REQUIRE(changes.back() == std::pair<std::int32_t, std::int32_t>{50, 55});
    cancel_next = true;
    invoke_fake_lives_write(&state, 7, reinterpret_cast<void*>(&fake_loss_lives_write));
    REQUIRE(state.lives == 50);
    REQUIRE(changes.back() == std::pair<std::int32_t, std::int32_t>{50, 43});
    invoke_fake_lives_write(&state, 5, reinterpret_cast<void*>(&fake_gain_lives_write));
    REQUIRE(state.lives == 55);

    hook.remove();
    REQUIRE_FALSE(hook.installed());
    changes.clear();
    invoke_fake_lives_write(&state, 4, reinterpret_cast<void*>(&fake_gain_lives_write));
    REQUIRE(state.lives == 59);
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
