// SPDX-License-Identifier: GPL-3.0-only
// btd5loader_validate: checks a mod package the way the loader would load
// it, without the game running. It validates the package layout and manifest,
// extracts it to a temporary folder, runs its Lua entry script with the
// loader's Lua host, and registers its custom towers with the loader's tower
// registry (checking art names against the game when --game is given).
//
// Usage: btd5loader_validate <package.btd5mod> [--game <game directory>]
// Prints one JSON object: {"valid": bool, "id": ..., "towers": [...], "errors": [...]}
// and exits 0 when valid, 1 when not, 2 on bad arguments.
#include <Windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "../runtime/custom_tower_art.hpp"
#include "../runtime/lua_mod.hpp"
#include "../runtime/mod_package.hpp"
#include "../runtime/tower_registry.hpp"

namespace {

using namespace btd5loader::runtime;

int report(const nlohmann::json& result) {
    std::cout << result.dump(2) << '\n';
    return result.value("valid", false) ? 0 : 1;
}

}  // namespace

int wmain(const int argc, wchar_t** const argv) {
    std::filesystem::path package_path;
    std::filesystem::path game_directory;
    for (int i = 1; i < argc; ++i) {
        const std::wstring argument = argv[i];
        if (argument == L"--game" && i + 1 < argc) {
            game_directory = argv[++i];
        } else if (package_path.empty()) {
            package_path = argument;
        } else {
            std::cerr << "usage: btd5loader_validate <package.btd5mod> [--game <game directory>]\n";
            return 2;
        }
    }
    if (package_path.empty()) {
        std::cerr << "usage: btd5loader_validate <package.btd5mod> [--game <game directory>]\n";
        return 2;
    }

    nlohmann::json result{{"valid", false}, {"towers", nlohmann::json::array()}, {"errors", nlohmann::json::array()}};
    std::string error;
    const auto package = validate_mod_package(package_path, error);
    if (!package) {
        result["errors"].push_back(error);
        return report(result);
    }
    result["id"] = package->manifest.id;

    std::error_code ignored;
    const auto workspace = std::filesystem::temp_directory_path(ignored) /
                           (L"btd5ml-validate-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(workspace, ignored);
    const auto resources = workspace / L"package";
    struct Cleanup final {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ignored_cleanup;
            std::filesystem::remove_all(path, ignored_cleanup);
        }
    } cleanup{workspace};
    if (!extract_mod_package(*package, resources, error)) {
        result["errors"].push_back(error);
        return report(result);
    }
    std::ifstream stream(resources / package->manifest.entry_point, std::ios::binary);
    if (!stream) {
        result["errors"].push_back("entry point " + package->manifest.entry_point + " is missing");
        return report(result);
    }
    std::stringstream script;
    script << stream.rdbuf();

    TowerRegistry registry;
    ArtCatalog catalog;
    if (!game_directory.empty()) {
        if (catalog.load(game_directory, error)) {
            registry.set_art_check([&catalog](const bool weapon_sprite, const std::string_view name) {
                return weapon_sprite ? catalog.has_weapon_sprite(name) : catalog.has_cell(name);
            });
        } else {
            result["warnings"].push_back("art names not checked: " + error);
        }
    }

    LuaModOptions options;
    options.mod_id = package->manifest.id;
    options.storage_directory = workspace / L"storage";
    options.resource_directory = resources;
    std::vector<std::string> logged;
    options.log = [&logged](const std::string_view level, const std::string_view message) {
        if (level == "error") logged.emplace_back(message);
    };
    options.register_tower = [&registry](const std::string_view id, CustomTowerSpec spec) {
        return registry.add(id, std::move(spec));
    };
    options.register_sound = [&registry](const std::string_view id, std::vector<std::filesystem::path> files,
                                 const double cooldown) { return registry.add_sound(id, std::move(files), cooldown); };
    LuaMod mod(options);
    if (!mod.valid() || !mod.load_script(script.str(), package->manifest.entry_point)) {
        const std::string message(mod.last_error());
        result["errors"].push_back(message.empty() ? "the Lua script failed to load" : message);
        return report(result);
    }
    for (const auto& message : logged) result["errors"].push_back(message);
    for (const auto& tower : registry.towers()) result["towers"].push_back(tower->id);
    result["sounds"] = nlohmann::json::array();
    for (const auto& sound : registry.sounds()) result["sounds"].push_back(sound->id);
    result["valid"] = result["errors"].empty();
    return report(result);
}
