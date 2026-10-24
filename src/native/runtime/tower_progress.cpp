// SPDX-License-Identifier: GPL-3.0-only
#include "tower_progress.hpp"

#include <cmath>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace btd5loader::runtime {
namespace {

constexpr int kVersion = 1;

}  // namespace

bool CustomTowerProgress::load(const std::filesystem::path& path, std::string& error) {
    std::lock_guard lock(mutex_);
    path_ = path;
    xp_.clear();
    dirty_ = false;
    std::error_code ignored;
    if (!std::filesystem::exists(path, ignored)) return true;
    std::ifstream stream(path, std::ios::binary);
    std::stringstream buffer;
    buffer << stream.rdbuf();
    try {
        const auto json = nlohmann::json::parse(buffer.str());
        if (json.value("version", 0) != kVersion) {
            error = "unsupported custom tower progress version";
            return false;
        }
        for (const auto& [id, entry] : json.at("towers").items()) {
            const double value = entry.value("xp", 0.0);
            if (std::isfinite(value) && value >= 0) xp_.emplace(id, std::make_unique<float>(static_cast<float>(value)));
        }
    } catch (const std::exception& exception) {
        xp_.clear();
        error = exception.what();
        return false;
    }
    return true;
}

bool CustomTowerProgress::save(std::string& error) {
    std::lock_guard lock(mutex_);
    if (!dirty_) return true;
    if (path_.empty()) {
        error = "custom tower progress has no file";
        return false;
    }
    nlohmann::json towers = nlohmann::json::object();
    for (const auto& [id, value] : xp_) towers[id] = {{"xp", *value}};
    const nlohmann::json json = {{"version", kVersion}, {"towers", std::move(towers)}};
    std::error_code ignored;
    std::filesystem::create_directories(path_.parent_path(), ignored);
    // Write a sibling file first so a crash mid-write keeps the old progress.
    const auto temporary = std::filesystem::path(path_).concat(L".tmp");
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream << json.dump(2);
        if (!stream) {
            error = "custom tower progress is unwritable";
            return false;
        }
    }
    std::error_code rename_error;
    std::filesystem::rename(temporary, path_, rename_error);
    if (rename_error) {
        error = rename_error.message();
        return false;
    }
    dirty_ = false;
    return true;
}

float CustomTowerProgress::xp(const std::string_view id) const {
    std::lock_guard lock(mutex_);
    const auto found = xp_.find(id);
    return found == xp_.end() ? 0.0F : *found->second;
}

void CustomTowerProgress::set_xp(const std::string_view id, const float value) {
    if (!std::isfinite(value) || value < 0) return;
    std::lock_guard lock(mutex_);
    const auto found = xp_.find(id);
    if (found != xp_.end()) {
        if (*found->second == value) return;
        *found->second = value;
    } else {
        xp_.emplace(std::string(id), std::make_unique<float>(value));
    }
    dirty_ = true;
}

}  // namespace btd5loader::runtime
