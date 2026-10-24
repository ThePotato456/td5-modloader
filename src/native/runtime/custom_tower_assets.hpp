// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace btd5loader::runtime {

class TowerRegistry;
struct CustomTower;
struct CustomTexture;

// Where generated custom tower files are written, relative to the game
// directory; the Asset policy's disk location resolves relative paths there.
inline constexpr std::string_view kGeneratedAssetRoot = "btd5loader_generated/";

enum class CustomAssetKind {
    CacheList,   // TowerDefinitions/CacheList.json plus every custom definition
    TowerOrder,  // the shop's TowerOrderTop.json plus a button per custom tower
    TowerFile,   // one of a custom tower's own files, built from its base tower's
    SpriteIndex, // Textures/ExtraSpriteInfo.xml plus every mod sprite sheet
    TextureFile, // a mod sprite sheet's image or atlas
    SoundList,   // Audio/game_sfx.json plus every mod sound effect
};

// What a TowerFile request holds beyond the base tower's copy.
enum class CustomAssetPart {
    Copy,           // the base tower's file, with definitions and upgrades customized
    StatsWeapon,    // the base tower's first weapon with the mod's stats applied
    ModWeapon,      // a weapon the mod defines (no vanilla source)
    UpgradeWeapon,  // a weapon an upgrade tier swaps in (no vanilla source)
    SpriteUpgrade,  // TowerSpriteUpgradeDefinitions/<native>.json for a tower with its own sprite
    TowerSprite,    // TowerSprites/btd5ml_<native>_<n>.json, one per sprite cell
};

struct CustomAssetRequest final {
    CustomAssetKind kind{};
    std::string source_path;             // vanilla file the content starts from; empty when there is none
    const CustomTower* tower{};          // for TowerFile
    CustomAssetPart part{};
    std::size_t index{};                 // ModWeapon: 1-based weapon; UpgradeWeapon: 1-based tier
    std::size_t path{};                  // UpgradeWeapon: 0 or 1
    const CustomTexture* texture{};      // TextureFile
    bool tablet{};                       // TextureFile: the Tablet-quality copy was asked for
    bool image{};                        // TextureFile: the PNG rather than the atlas
};

// True for the files every custom tower adds to (the tower cache list and the
// shop order). The game reads them once, so they must not be built before mods
// have finished registering towers.
[[nodiscard]] bool is_shared_custom_asset(std::string_view path);

// Classifies a path the game asks the Asset policy for. Returns nothing for
// files the loader does not serve, including every file when no custom tower
// is registered.
[[nodiscard]] std::optional<CustomAssetRequest> classify_custom_asset(
    std::string_view path, const TowerRegistry& registry);

// How a text lookup for a custom tower's string ID is answered: either the
// mod's own text, or the same ID for the base tower.
struct CustomTextLookup final {
    std::optional<std::string> text;
    std::string base_key;
};

// Resolves a CLoc string ID that names a registered custom tower (for example
// "LOC_<native>_TOWER" or "LOC_UPGRADE_A1_<native>"); custom upgrade names
// and descriptions come from the tower's upgrade tiers. Returns nothing for
// every other ID.
[[nodiscard]] std::optional<CustomTextLookup> resolve_custom_text(
    std::string_view key, const TowerRegistry& registry);

// Builds the served file from the vanilla source text. Returns nothing and
// sets `error` when the source cannot be transformed.
[[nodiscard]] std::optional<std::string> build_custom_asset(const CustomAssetRequest& request,
    std::string_view vanilla, const TowerRegistry& registry, std::string& error);

}  // namespace btd5loader::runtime
