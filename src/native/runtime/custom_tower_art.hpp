// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>

namespace btd5loader::runtime {

// The game's art names a custom tower may refer to: sprite cells (from the
// loose Ultra atlases under Assets/Textures) and weapon sprite files (the
// Assets/JSON/WeaponSprites entries of the asset archive, whose names are
// readable without its password).
class ArtCatalog final {
public:
    // Reads the catalog from the game directory. Returns false and sets
    // `error` when either source is missing (the catalog then stays empty).
    [[nodiscard]] bool load(const std::filesystem::path& game_directory, std::string& error);

    [[nodiscard]] bool loaded() const noexcept { return !cells_.empty(); }
    [[nodiscard]] bool has_cell(std::string_view name) const { return cells_.contains(std::string(name)); }
    [[nodiscard]] bool has_weapon_sprite(std::string_view name) const {
        return weapon_sprites_.contains(std::string(name));
    }

    // Adds atlas cells read from XML text (also used by tests).
    void add_cells_from_atlas(std::string_view atlas);
    void add_weapon_sprite(std::string name) { weapon_sprites_.insert(std::move(name)); }

private:
    std::unordered_set<std::string> cells_;
    std::unordered_set<std::string> weapon_sprites_;
};

}  // namespace btd5loader::runtime
