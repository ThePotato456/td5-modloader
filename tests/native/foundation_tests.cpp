#include <btd5loader/version.hpp>
#include <btd5loader/runtime_api.hpp>
#include <catch2/catch_test_macros.hpp>

#include <Windows.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../../src/native/runtime/compatibility.hpp"
#include "../../src/native/runtime/hook_transaction.hpp"
#include "../../src/native/runtime/pattern.hpp"
#include "../../src/native/runtime/runtime_state.hpp"

TEST_CASE("product metadata is available", "[foundation]") {
    REQUIRE(btd5loader::kProductName == "BTD5 Mod Loader");
    REQUIRE_FALSE(btd5loader::kVersion.empty());
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
