// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <string>

#include <nlohmann/json.hpp>

#include "tower_registry.hpp"

namespace btd5loader::runtime {

// A custom tower's gameplay content: stat overrides, weapons and upgrade
// paths, written into the tower's generated definition, weapon and upgrade
// files in the game's own JSON vocabulary.
//
// Weapons are .weapon objects: Type (a weapon type, default "DartMonkey"),
// TargetRange, CooldownTime, optional FireDelayTime, and a Tasks tree whose
// tasks are the game's task types (Projectile, Damage, AreaOfEffect,
// StatusEffect, ...), nested as child attacks through their own Tasks.
//
// Upgrade tiers: { name, cost, description, range = +n, cooldown = +n,
// pierce = +n, camo = true, weapon = <weapon>, tower_upgrade = {...},
// weapon_upgrade = {...} }; the friendly fields become the game's
// WeaponUpgrade Adjust / TaskUpgrade and TowerUpgrade Replace entries.

inline constexpr std::size_t kMaximumCustomWeapons = 8;
inline constexpr std::size_t kUpgradeTiers = 4;

// Loads the sheet `name` from `<directory>/<name>.png` and `.xml` (and
// `<directory>/Tablet/<name>.*` when present), checking the atlas names the
// sheet and listing its cells.
[[nodiscard]] std::string load_custom_texture(
    const std::filesystem::path& directory, const std::string& name, CustomTexture& texture);

// The in-game sprite cells a tower uses, base sprite first (empty when it
// keeps its base tower's sprite).
[[nodiscard]] std::vector<std::string> custom_sprite_cells(const CustomTowerSpec& spec);
// TowerSprites/<file> for a sprite cell index, and the generated sprite and
// sprite-upgrade definitions.
[[nodiscard]] std::string custom_sprite_file(const CustomTower& tower, std::size_t index);
[[nodiscard]] nlohmann::ordered_json build_tower_sprite(const CustomTower& tower, std::size_t index);
[[nodiscard]] nlohmann::ordered_json build_sprite_upgrade_definition(const CustomTower& tower);

// Generated file names under WeaponDefinitions/<native name>/.
[[nodiscard]] std::string custom_weapon_file(std::size_t index);                    // btd5ml_w<n>.weapon, 1-based
[[nodiscard]] std::string custom_upgrade_weapon_file(std::size_t path, std::size_t tier);  // btd5ml_u<a|b><tier>.weapon
[[nodiscard]] std::string stats_weapon_file(std::string_view base_file);            // btd5ml_p_<base file>

// Read from the JSON a mod's Lua tables convert to. Each returns an empty
// string on success or an error naming the bad field.
[[nodiscard]] std::string parse_tower_stats(const nlohmann::ordered_json& json, CustomTowerStats& stats);
[[nodiscard]] std::string parse_tower_weapon(const nlohmann::ordered_json& json, nlohmann::ordered_json& weapon);
[[nodiscard]] std::string parse_upgrade_tier(const nlohmann::ordered_json& json, CustomUpgrade& tier);
// A sprite: a cell name, or { frames = { cells... }, length = seconds } for a
// firing animation that shows each frame in turn (the first one at rest).
[[nodiscard]] std::string parse_sprite(const nlohmann::ordered_json& json, std::string& sprite,
    std::vector<std::string>& frames, double& length);

// An art name a spec uses: a sprite cell (GraphicName in weapons, icons,
// avatars, sprites) or a weapon sprite file (SpriteFile), and where.
struct ArtReference final {
    bool weapon_sprite{};
    std::string name;
    std::string where;
};
[[nodiscard]] std::vector<ArtReference> collect_art_references(const CustomTowerSpec& spec);

// Checks a whole spec's content (also run by TowerRegistry::add).
[[nodiscard]] std::string validate_tower_content(const CustomTowerSpec& spec);

// Generated files. `definition`, `weapon` and `upgrades` are the base tower's
// parsed vanilla files.
void customize_tower_definition(nlohmann::ordered_json& definition, const CustomTower& tower);
void apply_weapon_stats(nlohmann::ordered_json& weapon, const CustomTowerStats& stats);
[[nodiscard]] std::string customize_upgrades(nlohmann::ordered_json& upgrades, const CustomTower& tower);

}  // namespace btd5loader::runtime
