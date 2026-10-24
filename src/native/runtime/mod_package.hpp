// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mod_manifest.hpp"

namespace btd5loader::runtime {

struct ModPackage final {
    ModManifest manifest;
    std::filesystem::path archive_path;
    std::vector<std::string> files;
};

struct ModPackageLimits final {
    std::uintmax_t maximum_archive_bytes{64U * 1024U * 1024U};
    std::uint64_t maximum_total_uncompressed_bytes{256U * 1024U * 1024U};
    std::uint64_t maximum_file_uncompressed_bytes{32U * 1024U * 1024U};
    unsigned int maximum_entries{4096};
};

[[nodiscard]] std::optional<ModPackage> validate_mod_package(
    const std::filesystem::path& archive_path,
    std::string& error,
    const ModPackageLimits& limits = {});
[[nodiscard]] bool extract_mod_package(
    const ModPackage& package,
    const std::filesystem::path& destination,
    std::string& error);

}  // namespace btd5loader::runtime
