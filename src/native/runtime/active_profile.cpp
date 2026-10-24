// SPDX-License-Identifier: GPL-3.0-only
#include "active_profile.hpp"

#include <fstream>
#include <iterator>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace btd5loader::runtime {
namespace {

std::filesystem::path path_from_utf8(const std::string& text) {
    return std::filesystem::path(
        std::u8string(
            reinterpret_cast<const char8_t*>(text.data()),
            reinterpret_cast<const char8_t*>(text.data() + text.size())));
}

}  // namespace

std::optional<ActiveProfile> load_active_profile(
    const std::filesystem::path& path,
    std::string& error) {
    std::error_code filesystem_error;
    const auto size = std::filesystem::file_size(path, filesystem_error);
    if (filesystem_error || size == 0 || size > 1024U * 1024U) {
        error = "active profile is missing, empty, or exceeds 1 MiB";
        return std::nullopt;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "active profile could not be opened";
        return std::nullopt;
    }
    const std::string text{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>{}};
    try {
        const auto document = nlohmann::json::parse(text);
        if (!document.is_object() || document.at("schemaVersion").get<unsigned int>() != 1U) {
            error = "active profile schema is invalid or unsupported";
            return std::nullopt;
        }
        ActiveProfile profile;
        profile.name = document.at("profile").get<std::string>();
        profile.build_id = document.at("buildId").get<std::string>();
        if (profile.name.empty() || profile.name.size() > 64 || profile.build_id.empty() ||
            profile.build_id.size() > 128) {
            error = "active profile identity is invalid";
            return std::nullopt;
        }
        std::unordered_set<std::string> ids;
        for (const auto& source : document.at("mods")) {
            ActiveMod mod;
            mod.id = source.at("id").get<std::string>();
            mod.version = source.at("version").get<std::string>();
            mod.archive_path = path_from_utf8(source.at("archivePath").get<std::string>());
            mod.configuration =
                source.at("configuration").get<std::unordered_map<std::string, std::string>>();
            if (mod.id.empty() || mod.id.size() > 128 || mod.version.empty() ||
                mod.version.size() > 128 || !mod.archive_path.is_absolute() ||
                mod.archive_path.extension() != L".btd5mod" || !ids.insert(mod.id).second) {
                error = "active profile contains an invalid or duplicate mod";
                return std::nullopt;
            }
            for (const auto& [key, value] : mod.configuration) {
                if (key.empty() || key.size() > 128 || value.size() > 64U * 1024U) {
                    error = "active profile contains invalid mod configuration";
                    return std::nullopt;
                }
            }
            profile.mods.push_back(std::move(mod));
        }
        return profile;
    } catch (const nlohmann::json::exception& exception) {
        error = std::string("active profile is malformed: ") + exception.what();
        return std::nullopt;
    }
}

}  // namespace btd5loader::runtime
