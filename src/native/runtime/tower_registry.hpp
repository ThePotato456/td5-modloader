// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace btd5loader::runtime {

// What a mod asks for when it registers a custom tower.
// How a custom tower behaves in modes that limit the shop's towers (such as
// some challenges): with its base tower, never limited, or always left out.
enum class RestrictedModes { base, always, never };

// Stat overrides on the base tower. Tower fields go into the generated
// definition; weapon fields into the tower's first default weapon.
struct CustomTowerStats final {
    std::optional<double> range;             // weapon TargetRange
    std::optional<double> cooldown;          // weapon CooldownTime (seconds between shots)
    std::optional<std::int64_t> pierce;      // first projectile's NumPersists
    std::optional<std::int64_t> damage;      // Amount of the first projectile's Damage tasks
    std::optional<double> projectile_speed;  // first projectile's Movement.Speed
    std::optional<double> footprint;         // PlacementRadius
    std::optional<bool> camo;                // CanTargetCamo
    std::string targeting;                   // TargetingMode: First, Last, Strong or Close
    std::string placement;                   // "land", "water" or "both"

    [[nodiscard]] bool touches_weapon() const noexcept {
        return range || cooldown || pierce || damage || projectile_speed;
    }
};

// One tier of a custom upgrade path. `effect` is the game's own upgrade entry
// without its Name (TowerUpgrade / WeaponUpgrade); `weapon`, when set, replaces
// weapon slot 0 when the tier is bought.
struct CustomUpgrade final {
    std::string name;
    std::int64_t cost{};
    std::string description;
    nlohmann::ordered_json effect = nlohmann::ordered_json::object();
    std::optional<nlohmann::ordered_json> weapon;
    std::string icon;    // upgrade button icon cell; empty keeps the base tower's
    std::string avatar;  // selected-tower picture cell after this tier; empty keeps the base tower's
    std::string sprite;  // in-game sprite cell from this tier on; empty keeps the previous one
    std::vector<std::string> sprite_frames;  // when animated: every frame, `sprite` first
    double sprite_length{0.2};               // seconds the firing animation takes
};

// A sprite sheet a mod ships: a PNG and its cell atlas (the game's
// <FrameInformation> XML), drawn at Ultra size. Optional Tablet-size copies
// are used at the lower texture quality; otherwise the Ultra files are.
struct CustomTexture final {
    std::string name;
    std::filesystem::path image;
    std::filesystem::path atlas;
    std::filesystem::path tablet_image;
    std::filesystem::path tablet_atlas;
    std::vector<std::string> cells;
};

struct CustomTowerSpec final {
    std::string base{"DartMonkey"};    // vanilla tower whose files, art and button it starts from
    std::optional<std::int64_t> cost;  // BaseCost override
    std::string name;                  // shop name; empty keeps the base tower's text
    std::string description;           // shop description; empty keeps the base tower's text
    bool shop_first{};                 // lead the shop instead of following the vanilla towers
    bool xp_progression{};             // earn XP to pass the base tower's upgrade gates; otherwise all are open
    RestrictedModes restricted_modes{RestrictedModes::base};
    std::int64_t unlock_rank{};        // player rank that unlocks the tower in the shop (0: from the start)
    CustomTowerStats stats;
    // Art: sheets the mod ships, and the cells used for the shop button icon,
    // the selected-tower picture and the in-game sprite. Empty keeps the base
    // tower's art.
    std::vector<CustomTexture> textures;
    std::string icon;
    std::string avatar;
    std::string sprite;
    std::vector<std::string> sprite_frames;  // when animated: every frame, `sprite` first
    double sprite_length{0.2};               // seconds the firing animation takes
    // Weapon definitions (the game's .weapon vocabulary) replacing the base
    // tower's default weapons; empty keeps the base tower's.
    std::vector<nlohmann::ordered_json> weapons;
    // Up to four tiers per path replacing the base tower's path; an empty
    // path keeps the base tower's upgrades.
    std::array<std::vector<CustomUpgrade>, 2> upgrades;
};

// A sound effect a mod registers: one or more WAV files the game picks from
// at random, appended to the game's in-match effect list (game_sfx.json) as
// `effect`, with files served as `file_names` from the package.
struct CustomSound final {
    std::string id;                            // "<modId>:<soundId>"
    std::string effect;                        // effect name in game_sfx.json
    std::vector<std::filesystem::path> files;  // the package's WAV files
    std::vector<std::string> file_names;       // names the game asks for under Assets/Audio/
    double cooldown{0.1};                      // seconds before the effect can play again
    std::size_t index{};                       // 1-based registration index
};

struct CustomTower final {
    std::string id;           // "<modId>:<towerId>", the stable public identity
    std::string native_name;  // filename-safe name used by the game's converter and files
    std::uint32_t index{};    // 1-based registration index
    std::uint64_t flag{};     // encoded native type value
    std::uint64_t base_flag{};  // the base tower's single-bit vanilla value
    CustomTowerSpec spec;
};

// Loader-owned registry of custom tower identities. Indexes follow
// registration order; native names are a hash of the public ID, so they stay
// the same when load order changes.
class TowerRegistry final {
public:
    static constexpr std::size_t kMaximumTextLength = 200;

    // Answers whether the game has a sprite cell (`weapon_sprite` false) or a
    // weapon sprite file (true) by that name. Without one, art names are not
    // checked against the game.
    using ArtCheck = std::function<bool(bool weapon_sprite, std::string_view name)>;
    void set_art_check(ArtCheck check) { art_check_ = std::move(check); }

    // Registers `id`. Returns an empty string on success, or an error message
    // when the ID is malformed or already registered, the base is not a
    // vanilla tower, the cost, text, stats, weapons or upgrades are invalid
    // (see validate_tower_content), the native name collides, or the registry
    // is full.
    [[nodiscard]] std::string add(std::string_view id, CustomTowerSpec spec = {});

    [[nodiscard]] const std::vector<std::unique_ptr<CustomTower>>& towers() const noexcept { return towers_; }

    // Registers a sound effect "<modId>:<soundId>" from WAV files. Returns an
    // empty string or an error.
    [[nodiscard]] std::string add_sound(std::string_view id, std::vector<std::filesystem::path> files, double cooldown);
    [[nodiscard]] const std::vector<std::unique_ptr<CustomSound>>& sounds() const noexcept { return sounds_; }
    [[nodiscard]] const CustomSound* find_sound(std::string_view id) const;
    // The package file for a served sound file name ("btd5ml_s<n>_<k>.wav").
    [[nodiscard]] const std::filesystem::path* find_sound_file(std::string_view name) const;
    // True when nothing is registered (no towers and no sounds).
    [[nodiscard]] bool empty() const noexcept { return towers_.empty() && sounds_.empty(); }

    [[nodiscard]] const CustomTower* find_id(std::string_view id) const;
    [[nodiscard]] const CustomTower* find_native_name(std::string_view name) const;
    [[nodiscard]] const CustomTower* find_flag(std::uint64_t flag) const;
    [[nodiscard]] std::size_t size() const noexcept { return towers_.size(); }

    // The `tower:type()` name for a native type value: the vanilla name for a
    // single vanilla bit, the public ID for a registered custom tower, and an
    // empty view otherwise.
    [[nodiscard]] std::string_view type_name(std::uint64_t flag) const;

    // Deterministic native name for a public ID: "CT" and 16 hex digits.
    [[nodiscard]] static std::string native_name_for(std::string_view id);
    [[nodiscard]] static bool valid_id(std::string_view id);

private:
    ArtCheck art_check_;
    std::vector<std::unique_ptr<CustomTower>> towers_;
    std::vector<std::unique_ptr<CustomSound>> sounds_;
    std::unordered_map<std::string, const CustomTower*> by_id_;
    std::unordered_map<std::string, const CustomTower*> by_native_name_;
};

}  // namespace btd5loader::runtime
