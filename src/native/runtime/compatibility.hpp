// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace btd5loader::runtime {

struct FileFingerprints final {
    std::string executable_sha256;
    std::string assets_sha256;
};

struct SymbolDefinition final {
    std::string name;
    std::string module;
    std::string section;
    std::string pattern;
    std::ptrdiff_t result_offset{};
    bool required{};
    std::vector<std::string> prerequisites;
    std::ptrdiff_t validation_offset{};
    std::string validation_pattern;
};

struct BuildDefinition final {
    std::string id;
    std::string display_name;
    std::string executable_sha256;
    std::string assets_sha256;
    std::vector<SymbolDefinition> symbols;
    std::filesystem::path source_path;
};

struct DetectionResult final {
    FileFingerprints fingerprints;
    std::optional<BuildDefinition> build;
    std::string error;
};

[[nodiscard]] std::optional<std::string> sha256_file(
    const std::filesystem::path& path,
    std::string& error);
[[nodiscard]] std::optional<BuildDefinition> load_build_definition(
    const std::filesystem::path& path,
    std::string& error);
[[nodiscard]] DetectionResult detect_build(
    const std::filesystem::path& game_directory,
    const std::filesystem::path& symbol_map_directory);

}  // namespace btd5loader::runtime
