// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace btd5loader::runtime {

// Loader-owned progression for custom towers, kept out of the game's
// Profile.save. While the game runs, custom tower XP lives in the game's own
// per-type XP map; this store seeds that map once and takes the values back
// before each profile save. The file is JSON keyed by the tower's public ID,
// so progress follows the ID when indexes or load order change, and entries
// for towers whose mod is missing are kept untouched.
class CustomTowerProgress final {
public:
    // Loads `path` if it exists; returns false and sets `error` when the file
    // is unreadable or malformed (progress then starts empty).
    [[nodiscard]] bool load(const std::filesystem::path& path, std::string& error);
    // Writes the file when any value changed since the last load or save.
    [[nodiscard]] bool save(std::string& error);

    [[nodiscard]] float xp(std::string_view id) const;
    void set_xp(std::string_view id, float value);
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
    mutable std::mutex mutex_;
    std::map<std::string, std::unique_ptr<float>, std::less<>> xp_;
    bool dirty_{};
};

}  // namespace btd5loader::runtime
