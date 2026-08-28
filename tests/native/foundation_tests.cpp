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
#include "../../src/native/runtime/lua_mod.hpp"
#include "../../src/native/runtime/mod_manifest.hpp"
#include "../../src/native/runtime/mod_package.hpp"
#include "../../src/native/runtime/pattern.hpp"
#include "../../src/native/runtime/runtime_state.hpp"
#include "../../src/native/runtime/symbol_resolver.hpp"

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
