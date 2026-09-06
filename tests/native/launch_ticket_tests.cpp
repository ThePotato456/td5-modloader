// SPDX-License-Identifier: GPL-3.0-only
#include <Windows.h>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <future>
#include <nlohmann/json.hpp>
#include "../../src/native/runtime/launch_ticket.hpp"

TEST_CASE("launch tickets bind explicit intent to one installation and one consumption", "[launch]") {
    using namespace btd5loader::runtime;
    const auto root = std::filesystem::temp_directory_path() /
        (L"btd5ml-ticket-tests-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root / L".btd5ml-launch");
    const std::wstring token = L"1234-123456789";
    const auto path = root / L".btd5ml-launch" / (token + L".json");
    const auto claimed = std::filesystem::path(path.wstring() + L".claimed");
    const nlohmann::json valid{
        {"schemaVersion", 1}, {"launchProtocol", 1}, {"mode", "modded"},
        {"processId", 1234}, {"processStartedAt", "123456789"}, {"issuedAt", 1000}, {"expiresAt", 1060},
        {"gameDirectory", root.string()}, {"dataRoot", root.string()},
        {"profile", "fixture"}, {"buildId", "fixture"}, {"mods", nlohmann::json::array()}};
    const auto write = [&](const nlohmann::json& document) {
        std::filesystem::remove(claimed);
        std::ofstream output(path);
        output << document.dump();
    };
    std::string error;
    write(valid);
    REQUIRE_FALSE(consume_launch_ticket(root, L"", 1000, error));
    REQUIRE(std::filesystem::exists(path));
    REQUIRE_FALSE(consume_launch_ticket(root, L"../escape", 1000, error));
    REQUIRE(consume_launch_ticket(root, token, 1001, error));
    REQUIRE_FALSE(consume_launch_ticket(root, token, 1001, error));
    for (const auto now : {999, 1060, 2000}) {
        write(valid);
        REQUIRE_FALSE(consume_launch_ticket(root, token, now, error));
    }
    for (const std::string field : {"processId", "processStartedAt", "mode", "gameDirectory", "dataRoot"}) {
        auto invalid = valid;
        invalid[field] = "wrong";
        write(invalid);
        REQUIRE_FALSE(consume_launch_ticket(root, token, 1001, error));
    }
    for (const auto& invalid_time : {nlohmann::json("1000"), nlohmann::json(1000.5), nlohmann::json(nullptr)}) {
        auto invalid = valid;
        invalid["issuedAt"] = invalid_time;
        write(invalid);
        REQUIRE_FALSE(consume_launch_ticket(root, token, 1001, error));
    }
    auto excessive = valid;
    excessive["expiresAt"] = 1061;
    write(excessive);
    REQUIRE_FALSE(consume_launch_ticket(root, token, 1001, error));
    write(valid);
    std::ofstream(path, std::ios::trunc) << "{malformed";
    REQUIRE_FALSE(consume_launch_ticket(root, token, 1001, error));
    write(valid);
    auto competing = std::async(std::launch::async, [&]() {
        std::string local_error;
        return consume_launch_ticket(root, token, 1001, local_error).has_value();
    });
    const bool winner = consume_launch_ticket(root, token, 1001, error).has_value();
    REQUIRE(winner != competing.get());
    write(valid);
    REQUIRE(consume_launch_ticket(root, token, 1001, error));
    std::filesystem::remove_all(root);
}
