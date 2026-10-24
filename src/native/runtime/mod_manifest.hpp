// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <compare>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace btd5loader::runtime {

struct SemanticVersion final {
    unsigned int major{};
    unsigned int minor{};
    unsigned int patch{};
    std::string prerelease;

    auto operator<=>(const SemanticVersion&) const = default;
};

struct ModDependency final {
    std::string id;
    std::string version_requirement;
};

struct ModManifest final {
    std::string id;
    std::string name;
    std::string author;
    SemanticVersion version;
    std::string version_text;
    std::string entry_point;
    unsigned int loader_api{};
    std::vector<std::string> supported_game_builds;
    std::vector<ModDependency> dependencies;
    std::vector<std::string> load_before;
    std::vector<std::string> load_after;
    std::vector<std::string> capabilities;
};

struct LoadOrderResult final {
    std::vector<std::string> ordered_ids;
    std::string error;
};

[[nodiscard]] std::optional<SemanticVersion> parse_semantic_version(std::string_view text);
[[nodiscard]] bool version_satisfies(
    const SemanticVersion& version,
    std::string_view requirement);
[[nodiscard]] std::optional<ModManifest> parse_mod_manifest(
    std::string_view json_text,
    std::string& error);
[[nodiscard]] LoadOrderResult resolve_load_order(
    const std::vector<ModManifest>& manifests,
    const std::unordered_map<std::string, std::size_t>& profile_order);

}  // namespace btd5loader::runtime
