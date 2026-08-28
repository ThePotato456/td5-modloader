// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace btd5loader::runtime {

struct ActiveMod final {
    std::string id;
    std::string version;
    std::filesystem::path archive_path;
    std::unordered_map<std::string, std::string> configuration;
};

struct ActiveProfile final {
    std::string name;
    std::string build_id;
    std::vector<ActiveMod> mods;
};

[[nodiscard]] std::optional<ActiveProfile> load_active_profile(
    const std::filesystem::path& path,
    std::string& error);

}  // namespace btd5loader::runtime
