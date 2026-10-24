// SPDX-License-Identifier: GPL-3.0-only
#include "tower_registry.hpp"

#include "custom_tower_content.hpp"

#include <algorithm>
#include <cstdio>
#include <regex>

#include "custom_tower_identity.hpp"
#include "object_types.hpp"

namespace btd5loader::runtime {
namespace {

// The mod part follows the manifest's mod ID rule (mod_manifest.cpp); the
// tower part is a short lower-case name.
const std::regex kTowerIdPattern(
    R"(^[a-z][a-z0-9]*(\.[a-z0-9][a-z0-9-]*)+:[a-z][a-z0-9_-]{0,63}$)");

bool valid_text(const std::string_view text) {
    if (text.size() > TowerRegistry::kMaximumTextLength) return false;
    return std::none_of(text.begin(), text.end(), [](const unsigned char c) { return c < 0x20; });
}

bool is_vanilla_name(const std::string_view name) {
    return std::find(kTowerTypeNames.begin(), kTowerTypeNames.end(), name) != kTowerTypeNames.end();
}

}  // namespace

bool TowerRegistry::valid_id(const std::string_view id) {
    return id.size() <= 200 && std::regex_match(id.begin(), id.end(), kTowerIdPattern);
}

std::string TowerRegistry::native_name_for(const std::string_view id) {
    // FNV-1a 64-bit.
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (const unsigned char c : id) {
        hash ^= c;
        hash *= 0x100000001B3ULL;
    }
    char text[24];
    std::snprintf(text, sizeof(text), "CT%016llX", static_cast<unsigned long long>(hash));
    return text;
}

std::string TowerRegistry::add(const std::string_view id, CustomTowerSpec spec) {
    if (!valid_id(id)) return "invalid custom tower ID '" + std::string(id) + "'";
    const std::string quoted = "custom tower '" + std::string(id) + "'";
    if (!is_vanilla_name(spec.base)) return quoted + " has unknown base '" + spec.base + "'";
    if (spec.cost && (*spec.cost < 0 || *spec.cost > 1'000'000)) return quoted + " cost is out of range";
    if (spec.unlock_rank < 0 || spec.unlock_rank > 100) return quoted + " unlock_rank must be from 0 to 100";
    if (!valid_text(spec.name) || !valid_text(spec.description)) {
        return quoted + " name or description is too long or has control characters";
    }
    if (auto error = validate_tower_content(spec); !error.empty()) return quoted + " " + error;
    if (art_check_) {
        for (const auto& reference : collect_art_references(spec)) {
            const bool own_cell = !reference.weapon_sprite &&
                std::any_of(spec.textures.begin(), spec.textures.end(), [&reference](const CustomTexture& texture) {
                    return std::find(texture.cells.begin(), texture.cells.end(), reference.name) != texture.cells.end();
                });
            if (own_cell || art_check_(reference.weapon_sprite, reference.name)) continue;
            return quoted + " " + reference.where + " names unknown " +
                   (reference.weapon_sprite ? "weapon sprite file '" : "sprite cell '") + reference.name + "'";
        }
    }
    for (const auto& texture : spec.textures) {
        for (const auto& other : towers_) {
            for (const auto& existing : other->spec.textures) {
                if (existing.name == texture.name && existing.image != texture.image) {
                    return quoted + " texture '" + texture.name + "' is already used by '" + other->id + "'";
                }
            }
        }
    }
    if (by_id_.contains(std::string(id))) return "duplicate custom tower ID '" + std::string(id) + "'";
    if (towers_.size() >= kMaxCustomTowers) return "too many custom towers";
    auto tower = std::make_unique<CustomTower>();
    tower->id = std::string(id);
    tower->native_name = native_name_for(id);
    tower->spec = std::move(spec);
    if (is_vanilla_name(tower->native_name) || by_native_name_.contains(tower->native_name)) {
        return "custom tower ID '" + tower->id + "' collides with native name " + tower->native_name;
    }
    tower->index = static_cast<std::uint32_t>(towers_.size() + 1);
    tower->flag = encode_custom_tower(tower->index);
    const auto base = std::find(kTowerTypeNames.begin(), kTowerTypeNames.end(), tower->spec.base);
    tower->base_flag = std::uint64_t{1} << static_cast<unsigned>(base - kTowerTypeNames.begin());
    by_id_.emplace(tower->id, tower.get());
    by_native_name_.emplace(tower->native_name, tower.get());
    towers_.push_back(std::move(tower));
    return {};
}

std::string TowerRegistry::add_sound(
    const std::string_view id, std::vector<std::filesystem::path> files, const double cooldown) {
    constexpr std::size_t kMaximumFiles = 8;
    constexpr std::uintmax_t kMaximumBytes = 8U * 1024U * 1024U;
    const std::string quoted = "sound '" + std::string(id) + "'";
    if (!valid_id(id)) return "invalid sound ID '" + std::string(id) + "'";
    if (find_sound(id) != nullptr) return "duplicate sound ID '" + std::string(id) + "'";
    if (files.empty() || files.size() > kMaximumFiles) return quoted + " needs 1 to 8 files";
    if (!(cooldown >= 0 && cooldown <= 10)) return quoted + " cooldown must be from 0 to 10 seconds";
    for (const auto& file : files) {
        std::error_code ignored;
        if (file.extension() != L".wav" || !std::filesystem::is_regular_file(file, ignored)) {
            return quoted + " file " + file.filename().string() + " is not a WAV file in the package";
        }
        if (std::filesystem::file_size(file, ignored) > kMaximumBytes) {
            return quoted + " file " + file.filename().string() + " is larger than 8 MB";
        }
    }
    auto sound = std::make_unique<CustomSound>();
    sound->id = std::string(id);
    sound->index = sounds_.size() + 1;
    sound->effect = "BTD5ML_SOUND_" + std::to_string(sound->index);
    for (std::size_t i = 0; i < files.size(); ++i) {
        sound->file_names.push_back("btd5ml_s" + std::to_string(sound->index) + "_" + std::to_string(i + 1) + ".wav");
    }
    sound->files = std::move(files);
    sound->cooldown = cooldown;
    sounds_.push_back(std::move(sound));
    return {};
}

const CustomSound* TowerRegistry::find_sound(const std::string_view id) const {
    for (const auto& sound : sounds_) {
        if (sound->id == id) return sound.get();
    }
    return nullptr;
}

const std::filesystem::path* TowerRegistry::find_sound_file(const std::string_view name) const {
    for (const auto& sound : sounds_) {
        for (std::size_t i = 0; i < sound->file_names.size(); ++i) {
            if (sound->file_names[i] == name) return &sound->files[i];
        }
    }
    return nullptr;
}

const CustomTower* TowerRegistry::find_id(const std::string_view id) const {
    const auto found = by_id_.find(std::string(id));
    return found == by_id_.end() ? nullptr : found->second;
}

const CustomTower* TowerRegistry::find_native_name(const std::string_view name) const {
    const auto found = by_native_name_.find(std::string(name));
    return found == by_native_name_.end() ? nullptr : found->second;
}

const CustomTower* TowerRegistry::find_flag(const std::uint64_t flag) const {
    const auto index = decode_custom_tower(flag);
    if (!index || *index > towers_.size()) return nullptr;
    return towers_[*index - 1].get();
}

std::string_view TowerRegistry::type_name(const std::uint64_t flag) const {
    if (const CustomTower* tower = find_flag(flag)) return tower->id;
    return type_flag_name(kTowerTypeNames, flag);
}

}  // namespace btd5loader::runtime
