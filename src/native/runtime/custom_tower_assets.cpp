// SPDX-License-Identifier: GPL-3.0-only
#include "custom_tower_assets.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "custom_tower_content.hpp"
#include "tower_registry.hpp"

namespace btd5loader::runtime {
namespace {

constexpr std::string_view kJsonRoot = "Assets/JSON/";
constexpr std::string_view kCacheList = "Assets/JSON/TowerDefinitions/CacheList.json";
constexpr std::string_view kTowerOrder = "Assets/JSON/ScreenDefinitions/TowerSelectionScreen/TowerOrderTop.json";
constexpr std::string_view kSpriteIndex = "Assets/Textures/ExtraSpriteInfo.xml";
constexpr std::string_view kSoundList = "Assets/JSON/Audio/game_sfx.json";
constexpr std::string_view kTextureRoot = "Assets/Textures/";
constexpr std::string_view kTowerSprites = "Assets/JSON/TowerSprites/btd5ml_";
constexpr std::string_view kSpriteUpgrades = "Assets/JSON/TowerSpriteUpgradeDefinitions/";
std::string upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return text;
}

// Replaces the "_"-separated token equal to `from` in a string ID.
std::optional<std::string> rename_token(const std::string& id, const std::string& from, const std::string& to) {
    std::size_t start = 0;
    while (start <= id.size()) {
        const std::size_t end = std::min(id.find('_', start), id.size());
        if (end - start == from.size() && id.compare(start, end - start, from) == 0) {
            return id.substr(0, start) + to + id.substr(end);
        }
        start = end + 1;
    }
    return std::nullopt;
}

// Replaces the first path component or file stem equal to `from` with `to`.
std::optional<std::string> rename_component(std::string_view path, std::string_view from, std::string_view to) {
    std::size_t start = kJsonRoot.size();
    while (start < path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::size_t end = slash == std::string_view::npos ? path.size() : slash;
        const std::string_view component = path.substr(start, end - start);
        const std::size_t dot = slash == std::string_view::npos ? component.find('.') : std::string_view::npos;
        const std::string_view stem = component.substr(0, dot);
        if (stem == from) {
            std::string renamed(path.substr(0, start));
            renamed += to;
            renamed += path.substr(start + stem.size());
            return renamed;
        }
        start = end + 1;
    }
    return std::nullopt;
}

// Falls back to text edits for a base definition the JSON parser rejects;
// only the identity, cost and gates can be set this way.
std::string customize_definition_text(std::string text, const CustomTower& tower) {
    const auto replace = [&text](const std::regex& pattern, const std::string& replacement) {
        text = std::regex_replace(text, pattern, replacement, std::regex_constants::format_first_only);
    };
    replace(std::regex(R"re("TypeName"\s*:\s*"[^"]*")re"), "\"TypeName\" : \"" + tower.native_name + "\"");
    if (tower.spec.cost) {
        replace(std::regex(R"re("BaseCost"\s*:\s*-?[0-9.]+)re"), "\"BaseCost\" : " + std::to_string(*tower.spec.cost));
    }
    replace(std::regex(R"re("RankToUnlock"\s*:\s*[0-9]+)re"),
        "\"RankToUnlock\" : " + std::to_string(tower.spec.unlock_rank));
    if (!tower.spec.xp_progression) {
        text = std::regex_replace(text, std::regex(R"re(\{\s*"Rank"\s*:\s*[0-9]+\s*,\s*"XP"\s*:\s*[0-9]+\s*\})re"),
            "{ \"Rank\" : 0, \"XP\" : 0 }");
    }
    return text;
}

bool has_content(const CustomTowerSpec& spec) {
    const auto& stats = spec.stats;
    return stats.touches_weapon() || stats.camo || stats.footprint || !stats.targeting.empty() ||
           !stats.placement.empty() || !spec.weapons.empty() || !spec.upgrades[0].empty() || !spec.upgrades[1].empty();
}

std::string customize_definition(const std::string_view vanilla, const CustomTower& tower) {
    nlohmann::ordered_json definition;
    try {
        definition = nlohmann::ordered_json::parse(vanilla, nullptr, true, true);
    } catch (const nlohmann::json::exception&) {
        if (has_content(tower.spec)) throw;
        return customize_definition_text(std::string(vanilla), tower);
    }
    customize_tower_definition(definition, tower);
    return definition.dump(4);
}

// Generated weapon files: "btd5ml_w<n>", "btd5ml_u<a|b><tier>" and
// "btd5ml_p_<base file>" (see custom_tower_content.hpp).
void classify_generated_weapon(CustomAssetRequest& request) {
    const std::size_t slash = request.source_path.rfind('/');
    const std::string directory = request.source_path.substr(0, slash + 1);
    const std::string file = request.source_path.substr(slash + 1);
    const auto& spec = request.tower->spec;
    if (file.starts_with("btd5ml_p_")) {
        request.part = CustomAssetPart::StatsWeapon;
        request.source_path = directory + file.substr(9);
        return;
    }
    if (!file.ends_with(".weapon")) return;
    const std::string stem = file.substr(0, file.size() - 7);
    std::size_t parsed = 0;
    try {
        if (stem.starts_with("btd5ml_w")) {
            const auto index = std::stoul(stem.substr(8), &parsed);
            if (parsed != stem.size() - 8 || index == 0 || index > spec.weapons.size()) return;
            request.part = CustomAssetPart::ModWeapon;
            request.index = index;
            request.source_path.clear();
        } else if (stem.size() == 10 && stem.starts_with("btd5ml_u") && (stem[8] == 'a' || stem[8] == 'b')) {
            const std::size_t path = stem[8] == 'a' ? 0 : 1;
            const auto tier = std::stoul(stem.substr(9), &parsed);
            if (tier == 0 || tier > spec.upgrades[path].size() || !spec.upgrades[path][tier - 1].weapon) return;
            request.part = CustomAssetPart::UpgradeWeapon;
            request.path = path;
            request.index = tier;
            request.source_path.clear();
        }
    } catch (const std::exception&) {
    }
}

std::string read_binary(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot read " + path.string());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

const CustomTexture* find_texture(const TowerRegistry& registry, const std::string_view name) {
    for (const auto& tower : registry.towers()) {
        for (const auto& texture : tower->spec.textures) {
            if (texture.name == name) return &texture;
        }
    }
    return nullptr;
}

bool has_textures(const TowerRegistry& registry) {
    return std::any_of(registry.towers().begin(), registry.towers().end(),
        [](const auto& tower) { return !tower->spec.textures.empty(); });
}

}  // namespace

bool is_shared_custom_asset(const std::string_view path) {
    return path == kCacheList || path == kTowerOrder || path == kSpriteIndex || path == kSoundList;
}

std::optional<CustomTextLookup> resolve_custom_text(const std::string_view key, const TowerRegistry& registry) {
    // Native names start with "CT"; skip the scan for every other ID.
    if (registry.size() == 0 || key.find("CT") == std::string_view::npos) return std::nullopt;
    const std::string id(key);
    for (const auto& tower : registry.towers()) {
        auto base_key = rename_token(id, tower->native_name, tower->spec.base);
        if (!base_key) continue;
        const auto& spec = tower->spec;
        const std::string& native = tower->native_name;
        CustomTextLookup lookup{std::nullopt, std::move(*base_key)};
        if (!spec.name.empty()) {
            if (id == "LOC_" + native + "_TOWER") lookup.text = spec.name;
            if (id == "LOC_" + native + "_TOWER_PLURAL") lookup.text = spec.name + "s";
            if (id == "LOC_" + native + "_TOWER_CAPS") lookup.text = upper(spec.name);
        }
        // Custom upgrade text: LOC_UPGRADE_<A|B><tier>_<native> and
        // LOC_<native>_UPGRADE_DESC_<A|B><tier>.
        for (std::size_t path = 0; path < spec.upgrades.size(); ++path) {
            for (std::size_t tier = 0; tier < spec.upgrades[path].size(); ++tier) {
                const std::string slot = std::string(1, path == 0 ? 'A' : 'B') + std::to_string(tier + 1);
                const auto& upgrade = spec.upgrades[path][tier];
                if (id == "LOC_UPGRADE_" + slot + "_" + native) lookup.text = upgrade.name;
                if (id == "LOC_" + native + "_UPGRADE_DESC_" + slot && !upgrade.description.empty()) {
                    lookup.text = upgrade.description;
                }
            }
        }
        // The Windows build asks for the "OSX" description variant.
        if (!spec.description.empty() && (id == "LOC_TOWER_DESC_" + native || id == "LOC_TOWER_DESC_OSX_" + native)) {
            lookup.text = spec.description;
        }
        return lookup;
    }
    return std::nullopt;
}

std::optional<CustomAssetRequest> classify_custom_asset(const std::string_view path, const TowerRegistry& registry) {
    if (registry.empty()) return std::nullopt;
    if (path == kSoundList) {
        if (registry.sounds().empty()) return std::nullopt;
        return CustomAssetRequest{CustomAssetKind::SoundList, std::string(path), nullptr};
    }
    if (registry.size() == 0) return std::nullopt;
    if (path.starts_with(kTextureRoot)) {
        if (!has_textures(registry)) return std::nullopt;
        if (path == kSpriteIndex) return CustomAssetRequest{CustomAssetKind::SpriteIndex, std::string(path), nullptr};
        // Assets/Textures/<quality>/<sheet>.png|.xml
        const auto slash = path.rfind('/');
        const auto file = path.substr(slash + 1);
        const bool image = file.ends_with(".png");
        if (!image && !file.ends_with(".xml")) return std::nullopt;
        const CustomTexture* texture = find_texture(registry, file.substr(0, file.size() - 4));
        if (texture == nullptr) return std::nullopt;
        CustomAssetRequest request{CustomAssetKind::TextureFile, {}, nullptr};
        request.texture = texture;
        request.image = image;
        request.tablet = path.substr(0, slash).ends_with("/Tablet");
        return request;
    }
    if (!path.starts_with(kJsonRoot)) return std::nullopt;
    if (path == kCacheList) return CustomAssetRequest{CustomAssetKind::CacheList, std::string(path), nullptr};
    if (path == kTowerOrder) return CustomAssetRequest{CustomAssetKind::TowerOrder, std::string(path), nullptr};
    // Native names start with "CT"; skip the scan for every other file.
    if (path.find("CT") == std::string_view::npos) return std::nullopt;
    for (const auto& tower : registry.towers()) {
        if (custom_sprite_cells(tower->spec).empty()) continue;
        if (path == std::string(kSpriteUpgrades) + tower->native_name + ".json") {
            CustomAssetRequest request{CustomAssetKind::TowerFile, {}, tower.get()};
            request.part = CustomAssetPart::SpriteUpgrade;
            return request;
        }
        const std::string prefix = std::string(kTowerSprites) + tower->native_name + "_";
        if (path.starts_with(prefix) && path.ends_with(".json")) {
            const auto digits = path.substr(prefix.size(), path.size() - prefix.size() - 5);
            if (digits.empty() || !std::all_of(digits.begin(), digits.end(), [](const char c) { return c >= '0' && c <= '9'; })) {
                return std::nullopt;
            }
            const auto index = std::stoul(std::string(digits));
            if (index >= custom_sprite_cells(tower->spec).size()) return std::nullopt;
            CustomAssetRequest request{CustomAssetKind::TowerFile, {}, tower.get()};
            request.part = CustomAssetPart::TowerSprite;
            request.index = index;
            return request;
        }
    }
    if (path.find("/CT") == std::string_view::npos) return std::nullopt;
    for (const auto& tower : registry.towers()) {
        if (auto source = rename_component(path, tower->native_name, tower->spec.base)) {
            CustomAssetRequest request{CustomAssetKind::TowerFile, std::move(*source), tower.get()};
            if (request.source_path.find("btd5ml_") != std::string::npos) classify_generated_weapon(request);
            return request;
        }
    }
    return std::nullopt;
}

std::optional<std::string> build_custom_asset(const CustomAssetRequest& request, const std::string_view vanilla,
    const TowerRegistry& registry, std::string& error) {
    try {
        switch (request.kind) {
        case CustomAssetKind::TowerFile: {
            const CustomTower& tower = *request.tower;
            switch (request.part) {
            case CustomAssetPart::ModWeapon:
                return tower.spec.weapons.at(request.index - 1).dump(4);
            case CustomAssetPart::UpgradeWeapon:
                return tower.spec.upgrades.at(request.path).at(request.index - 1).weapon.value().dump(4);
            case CustomAssetPart::StatsWeapon: {
                auto weapon = nlohmann::ordered_json::parse(vanilla, nullptr, true, true);
                apply_weapon_stats(weapon, tower.spec.stats);
                return weapon.dump(4);
            }
            case CustomAssetPart::SpriteUpgrade:
                return build_sprite_upgrade_definition(tower).dump(4);
            case CustomAssetPart::TowerSprite:
                return build_tower_sprite(tower, request.index).dump(4);
            case CustomAssetPart::Copy:
                break;
            }
            if (request.source_path.ends_with(".tower")) return customize_definition(vanilla, tower);
            if (request.source_path.ends_with(".upgrades") &&
                (!tower.spec.upgrades[0].empty() || !tower.spec.upgrades[1].empty())) {
                auto upgrades = nlohmann::ordered_json::parse(vanilla, nullptr, true, true);
                if (auto failure = customize_upgrades(upgrades, tower); !failure.empty()) {
                    error = failure;
                    return std::nullopt;
                }
                return upgrades.dump(4);
            }
            return std::string(vanilla);
        }
        case CustomAssetKind::CacheList: {
            auto json = nlohmann::json::parse(vanilla, nullptr, true, true);
            auto& files = json.at("Files");
            for (const auto& tower : registry.towers()) files.push_back(tower->native_name + ".tower");
            return json.dump(4);
        }
        case CustomAssetKind::TowerOrder: {
            auto json = nlohmann::json::parse(vanilla, nullptr, true, true);
            auto& items = json.at("Items");
            if (!items.is_array() || items.empty()) {
                error = "tower order has no items";
                return std::nullopt;
            }
            nlohmann::json first = nlohmann::json::array();
            nlohmann::json last = nlohmann::json::array();
            for (const auto& tower : registry.towers()) {
                // Start from the base tower's button so the icon matches.
                nlohmann::json item = items.front();
                for (const auto& candidate : items) {
                    if (candidate.value("FactoryName", "") == tower->spec.base) item = candidate;
                }
                item["FactoryName"] = tower->native_name;
                item["KeyboardShortcut"] = "";
                if (!tower->spec.icon.empty()) item["Icon"] = tower->spec.icon;
                (tower->spec.shop_first ? first : last).push_back(std::move(item));
            }
            items.insert(items.begin(), first.begin(), first.end());
            items.insert(items.end(), last.begin(), last.end());
            return json.dump(4);
        }
        case CustomAssetKind::SpriteIndex: {
            std::string index(vanilla);
            const auto end = index.rfind("</SpriteInformation>");
            if (end == std::string::npos) {
                error = "sprite index has no </SpriteInformation>";
                return std::nullopt;
            }
            std::string sheets;
            std::vector<std::string> added;
            for (const auto& tower : registry.towers()) {
                for (const auto& texture : tower->spec.textures) {
                    if (std::find(added.begin(), added.end(), texture.name) != added.end()) continue;
                    added.push_back(texture.name);
                    sheets += "    <SpriteInfoXml name=\"" + texture.name + "\" type=\"png\"/>\n";
                }
            }
            index.insert(end, sheets);
            return index;
        }
        case CustomAssetKind::SoundList: {
            // Mod effects go last, so they are the last entries of the
            // manager's effect list (see CustomTowerHooks::play_sound).
            auto json = nlohmann::ordered_json::parse(vanilla, nullptr, true, true);
            auto& effects = json["SFX"];
            if (!effects.is_array()) {
                error = "sound list has no SFX list";
                return std::nullopt;
            }
            for (const auto& sound : registry.sounds()) {
                effects.push_back(nlohmann::ordered_json::array({sound->effect, sound->cooldown, sound->file_names}));
            }
            return json.dump(4);
        }
        case CustomAssetKind::TextureFile: {
            const CustomTexture& texture = *request.texture;
            const bool tablet = request.tablet && !texture.tablet_image.empty();
            if (request.image) return read_binary(tablet ? texture.tablet_image : texture.image);
            return read_binary(tablet ? texture.tablet_atlas : texture.atlas);
        }
        }
    } catch (const std::exception& exception) {
        error = exception.what();
        return std::nullopt;
    }
    error = "unknown custom asset kind";
    return std::nullopt;
}

}  // namespace btd5loader::runtime
