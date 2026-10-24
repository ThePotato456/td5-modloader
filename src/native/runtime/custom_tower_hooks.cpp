// SPDX-License-Identifier: GPL-3.0-only
#include "custom_tower_hooks.hpp"

#include <Windows.h>

#include <MinHook.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "custom_tower_assets.hpp"
#include "tower_progress.hpp"
#include "tower_registry.hpp"

namespace btd5loader::runtime {
namespace {

constexpr int kTowerCategory = 0;
constexpr int kOpenForReading = 1;

using NameToFlag = unsigned long long(__thiscall*)(void*, int, const void*);
using FlagToName = void*(__thiscall*)(void*, void*, int, std::uint32_t, std::uint32_t);
using OpenFile = void*(__thiscall*)(void*, const void*, const void*, int);
using ConstructString = void*(__thiscall*)(void*, const char*);
using DestroyString = void(__thiscall*)(void*);
using FlagLookup = bool(__thiscall*)(void*, const unsigned long long*);
using ShopUnlocked = bool(__thiscall*)(void*, void*);
using UpgradeUnlocked = bool(__thiscall*)(void*, const unsigned long long*, unsigned, unsigned);
using ProfileSave = void(__thiscall*)(void*, void*, void*, void*);
using TowerXp = float*(__thiscall*)(void*, const unsigned long long*);
using TextLookup = void*(__thiscall*)(void*, void*, const void*, void*);
using XpAward = void(__thiscall*)(void*, int, int);

NameToFlag g_name_to_flag{};
FlagToName g_flag_to_name{};
OpenFile g_open_file{};
ConstructString g_construct{};
DestroyString g_destroy{};
FlagLookup g_profile_unlocked{};
ShopUnlocked g_shop_unlocked{};
FlagLookup g_shop_available{};
UpgradeUnlocked g_upgrade_unlocked{};
ProfileSave g_profile_save{};
TowerXp g_tower_xp{};
TextLookup g_text_lookup{};
XpAward g_xp_award{};
using FileExists = bool(__thiscall*)(void*, const void*, const void*);
FileExists g_file_exists{};
using LoadSpriteIndex = void(__thiscall*)(void*, const void*, void*, int);
using GetSprite = void*(__thiscall*)(void*, const void*, const void*);
LoadSpriteIndex g_load_sprite_index{};
GetSprite g_get_sprite{};
using LoadTexture = void(__thiscall*)(void*, const void*, int);
LoadTexture g_load_texture{};
using SfxInstance = void*(__cdecl*)();
using SfxPlay = void(__thiscall*)(void*, int);
using MmioOpen = void*(WINAPI*)(wchar_t*, void*, unsigned long);
SfxInstance g_sfx_instance{};
SfxPlay g_sfx_play{};
MmioOpen g_mmio_open{};
void* g_mmio_target{};
CustomTowerProgress* g_progress{};
// Custom tower XP tracking. During play a custom tower's XP lives in a node of
// the game's per-type XP map; the game sometimes rebuilds that map (for
// example when a match ends), dropping the node, and its XP award routine
// (0x632920) can then insert a fresh node holding only XP earned since. Each
// accessor result is checked against the last node and value seen: a new node
// or a lower value means a rebuild, so the last known total is added back.
struct XpTrack final {
    float* node{};
    float total{};
};
void* g_xp_profile{};  // profile object whose XP map the game last used
std::unordered_map<std::uint32_t, XpTrack> g_xp_tracks;  // by custom index
bool g_xp_tracking_started = false;
// Set once the XP hooks are installed for towers that use XP progression.
bool g_xp_enabled = false;
void* g_xp_accessor_target{};
void* g_xp_award_target{};
std::mutex g_registration_mutex;
std::condition_variable g_registration_done;
bool g_registration_finished = false;
constexpr std::chrono::seconds kRegistrationWait{20};
// Nesting depth of the profile writer on this thread.
thread_local int t_profile_save_depth = 0;
constexpr const char* kVanillaPlaceholderName = "INVALID";
// IFile vtable slots, from CJSONWrapper::ReadValue (0x9A79F0).
using FileSize = std::uint32_t(__thiscall*)(void*);
using FileRead = void(__thiscall*)(void*, void*, std::uint32_t, int);
using FileClose = void(__thiscall*)(void*, int);
constexpr std::size_t kFileDestroySlot = 0;
constexpr std::size_t kFileCloseSlot = 1;
constexpr std::size_t kFileSizeSlot = 3;
constexpr std::size_t kFileReadSlot = 8;
constexpr std::uint32_t kMaximumAssetBytes = 16U * 1024U * 1024U;

const TowerRegistry* g_registry{};
CustomTowerHooks::Log g_log;
std::filesystem::path g_game_directory;
std::mutex g_generated_mutex;
std::unordered_set<std::string> g_generated;
// Custom text IDs already reported, so each is logged once.
std::unordered_set<std::string> g_logged_text;
std::array<void*, 15> g_installed{};
// Research diagnostic (BTD5ML_DIAG_OPEN_FILES): logs file opens and existence checks.
bool g_diag_open_files = false;
std::chrono::steady_clock::time_point g_diag_start;
// Core hooks take the first slots (the texture index hook is optional); the
// XP hooks follow them.
constexpr std::size_t kXpHookSlot = 13;


// Reads an MSVC std::string (16-byte buffer, size at +0x10, capacity at +0x14).
std::string_view read_string(const void* const text) {
    const auto* const bytes = static_cast<const std::uint8_t*>(text);
    std::uint32_t size = 0;
    std::uint32_t capacity = 0;
    std::memcpy(&size, bytes + 0x10, sizeof(size));
    std::memcpy(&capacity, bytes + 0x14, sizeof(capacity));
    if (size > 4096) return {};
    const char* data = reinterpret_cast<const char*>(bytes);
    if (capacity > 15) std::memcpy(&data, bytes, sizeof(data));
    return {data, size};
}

bool is_custom(const unsigned long long* const flag) {
    const TowerRegistry* const registry = g_registry;
    return registry != nullptr && flag != nullptr && registry->find_flag(*flag) != nullptr;
}

unsigned long long __fastcall hooked_name_to_flag(
    void* const converter, void*, const int category, const void* const name) noexcept {
    const TowerRegistry* const registry = g_registry;
    if (registry != nullptr && category == kTowerCategory && name != nullptr) {
        if (const CustomTower* tower = registry->find_native_name(read_string(name))) return tower->flag;
    }
    return g_name_to_flag(converter, category, name);
}

void* __fastcall hooked_flag_to_name(void* const converter, void*, void* const out, const int category,
    const std::uint32_t low, const std::uint32_t high) noexcept {
    const TowerRegistry* const registry = g_registry;
    if (registry != nullptr) {
        const auto flag = (static_cast<unsigned long long>(high) << 32) | low;
        if (const CustomTower* tower = registry->find_flag(flag)) {
            if (category == kTowerCategory) {
                return g_construct(out, t_profile_save_depth > 0 ? kVanillaPlaceholderName : tower->native_name.c_str());
            }
            // Other categories hold per-tower properties indexed by the same
            // bits (category 4 says whether a tower is a Pro agent, which the
            // XP award uses to pick the XP key); answer as the base tower.
            const auto base = tower->base_flag;
            return g_flag_to_name(converter, out, category, static_cast<std::uint32_t>(base),
                static_cast<std::uint32_t>(base >> 32));
        }
    }
    return g_flag_to_name(converter, out, category, low, high);
}

void log_line(const std::string& line) noexcept {
    if (!g_log) return;
    try {
        g_log(line);
    } catch (...) {
    }
}

template <typename Function>
Function file_slot(void* const file, const std::size_t slot) {
    return reinterpret_cast<Function>((*static_cast<void***>(file))[slot]);
}

// Opens `path` through the original OpenFile, with the string built by the
// game's own code so its allocator owns any heap buffer.
void* open_original(void* const io, const std::string& path, const void* const policy) {
    alignas(4) std::array<std::uint8_t, 0x18> text{};
    g_construct(text.data(), path.c_str());
    void* const file = g_open_file(io, text.data(), policy, kOpenForReading);
    g_destroy(text.data());
    return file;
}

std::optional<std::string> read_original(void* const io, const std::string& path, const void* const policy) {
    void* const file = open_original(io, path, policy);
    if (file == nullptr) return std::nullopt;
    const std::uint32_t size = file_slot<FileSize>(file, kFileSizeSlot)(file);
    std::optional<std::string> text;
    if (size <= kMaximumAssetBytes) {
        text.emplace(size, '\0');
        file_slot<FileRead>(file, kFileReadSlot)(file, text->data(), size, 0);
    }
    file_slot<FileClose>(file, kFileCloseSlot)(file, 0);
    file_slot<FileClose>(file, kFileDestroySlot)(file, 1);
    return text;
}

// Builds the requested custom asset from its vanilla source once per session,
// writes it under the generated root, and opens that copy.
void* serve_custom_asset(void* const io, const std::string& path, const CustomAssetRequest& request,
    const void* const policy) {
    const std::string generated = std::string(kGeneratedAssetRoot) + path;
    std::lock_guard lock(g_generated_mutex);
    if (!g_generated.contains(path)) {
        const auto vanilla = request.source_path.empty() ? std::optional<std::string>(std::string())
                                                          : read_original(io, request.source_path, policy);
        if (!vanilla) {
            log_line("custom_tower_asset_missing " + request.source_path);
            return nullptr;
        }
        std::string error;
        const auto content = build_custom_asset(request, *vanilla, *g_registry, error);
        if (!content) {
            log_line("custom_tower_asset_failed " + path + ": " + error);
            return nullptr;
        }
        const auto target = g_game_directory / std::filesystem::path(generated);
        std::error_code ignored;
        std::filesystem::create_directories(target.parent_path(), ignored);
        std::ofstream stream(target, std::ios::binary | std::ios::trunc);
        stream.write(content->data(), static_cast<std::streamsize>(content->size()));
        if (!stream) {
            log_line("custom_tower_asset_unwritable " + target.string());
            return nullptr;
        }
        g_generated.insert(path);
        log_line("custom_tower_asset " + path);
    }
    return open_original(io, generated, policy);
}

void* __fastcall hooked_open_file(
    void* const io, void*, const void* const path, const void* const policy, const int mode) noexcept {
    const TowerRegistry* const registry = g_registry;
    if (g_diag_open_files && path != nullptr) {
        try {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - g_diag_start);
            log_line("diag_open_file t=" + std::to_string(elapsed.count()) + " mode=" + std::to_string(mode) +
                     " registered=" + (g_registration_finished ? "1" : "0") + " " + std::string(read_string(path)));
        } catch (...) {
        }
    }
    if (registry != nullptr && mode == kOpenForReading && path != nullptr) {
        try {
            const std::string requested(read_string(path));
            if (is_shared_custom_asset(requested)) {
                // The game reads these once; build them with every mod's towers.
                std::unique_lock lock(g_registration_mutex);
                if (!g_registration_done.wait_for(lock, kRegistrationWait, [] { return g_registration_finished; })) {
                    log_line("custom_tower_registration_wait_timed_out " + requested);
                }
            }
            if (const auto request = classify_custom_asset(requested, *registry)) {
                if (void* const file = serve_custom_asset(io, requested, *request, policy)) return file;
            }
        } catch (...) {
            // Fall back to the vanilla file.
        }
    }
    return g_open_file(io, path, policy, mode);
}

// The game checks that some files exist before opening them (sprite sheets
// among them). Files the loader builds without a vanilla source exist only
// through the OpenFile hook, so answer for them here.
bool __fastcall hooked_file_exists(void* const io, void*, const void* const path, const void* const policy) noexcept {
    const TowerRegistry* const registry = g_registry;
    if (g_diag_open_files && path != nullptr) {
        try {
            log_line("diag_file_exists " + std::string(read_string(path)));
        } catch (...) {
        }
    }
    if (registry != nullptr && path != nullptr) {
        try {
            const auto request = classify_custom_asset(read_string(path), *registry);
            if (request && request->source_path.empty()) return true;
        } catch (...) {
        }
    }
    return g_file_exists(io, path, policy);
}

// Mod sprite sheets are listed in ExtraSpriteInfo.xml, but the game loads a
// sheet's image only when code asks for it by name. After the index is read,
// do what the game does for its cursor sheet: take a sprite from the sheet
// (which keeps it referenced) and load the sheet's image (flag 1).
// Mod cell name -> its sheet, built once registration has finished.
std::once_flag g_cell_sheets_once;
std::unordered_map<std::string, std::string> g_cell_sheets;

const std::unordered_map<std::string, std::string>& cell_sheets() {
    std::call_once(g_cell_sheets_once, [] {
        if (g_registry == nullptr) return;
        for (const auto& tower : g_registry->towers()) {
            for (const auto& texture : tower->spec.textures) {
                for (const auto& cell : texture.cells) g_cell_sheets.emplace(cell, texture.name);
            }
        }
    });
    return g_cell_sheets;
}

// HUD code asks for icons from fixed sheets (such as in_game_hud and
// upgrade_icons). A cell that belongs to a mod sheet is looked up in that
// sheet instead, so a mod's icons and avatars draw wherever vanilla ones do.
void* __fastcall hooked_get_sprite(void* const textures, void*, const void* const sheet, const void* const cell) noexcept {
    if (g_registration_finished && cell != nullptr) {
        try {
            const auto& sheets = cell_sheets();
            if (!sheets.empty()) {
                const auto found = sheets.find(std::string(read_string(cell)));
                if (found != sheets.end() && read_string(sheet) != found->second) {
                    alignas(4) std::array<std::uint8_t, 0x18> text{};
                    g_construct(text.data(), found->second.c_str());
                    void* const sprite = g_get_sprite(textures, text.data(), cell);
                    g_destroy(text.data());
                    return sprite;
                }
            }
        } catch (...) {
        }
    }
    return g_get_sprite(textures, sheet, cell);
}

void __fastcall hooked_load_sprite_index(
    void* const textures, void*, const void* const path, void* const out, const int flag) noexcept {
    g_load_sprite_index(textures, path, out, flag);
    const TowerRegistry* const registry = g_registry;
    if (registry == nullptr || g_get_sprite == nullptr || path == nullptr) return;
    try {
        if (read_string(path) != "Assets/Textures/ExtraSpriteInfo.xml") return;
        std::vector<std::string> loaded;
        for (const auto& tower : registry->towers()) {
            for (const auto& texture : tower->spec.textures) {
                if (texture.cells.empty() ||
                    std::find(loaded.begin(), loaded.end(), texture.name) != loaded.end()) {
                    continue;
                }
                loaded.push_back(texture.name);
                alignas(4) std::array<std::uint8_t, 0x18> sheet{};
                alignas(4) std::array<std::uint8_t, 0x18> cell{};
                g_construct(sheet.data(), texture.name.c_str());
                g_construct(cell.data(), texture.cells.front().c_str());
                void* const sprite = g_get_sprite(textures, sheet.data(), cell.data());
                if (sprite != nullptr && g_load_texture != nullptr) g_load_texture(textures, sheet.data(), 1);
                g_destroy(cell.data());
                g_destroy(sheet.data());
                log_line("custom_tower_texture sheet=" + texture.name + (sprite != nullptr ? " loaded" : " missing"));
            }
        }
    } catch (...) {
    }
}

// The sound manager opens WAV files itself (Assets/Audio/<name> through
// mmioOpenW). Names the loader gave mod sound files open the package file.
void* WINAPI hooked_mmio_open(wchar_t* const path, void* const info, const unsigned long flags) noexcept {
    const TowerRegistry* const registry = g_registry;
    if (registry != nullptr && path != nullptr && !registry->sounds().empty()) {
        try {
            const std::wstring_view text(path);
            const auto slash = text.find_last_of(L"/\\");
            const auto name = slash == std::wstring_view::npos ? text : text.substr(slash + 1);
            if (name.starts_with(L"btd5ml_s")) {
                std::string narrow;
                for (const wchar_t c : name) narrow.push_back(static_cast<char>(c));
                if (const auto* file = registry->find_sound_file(narrow)) {
                    std::wstring redirected = file->wstring();
                    log_line("custom_sound_file " + narrow);
                    return g_mmio_open(redirected.data(), info, flags);
                }
            }
        } catch (...) {
        }
    }
    return g_mmio_open(path, info, flags);
}

bool __fastcall hooked_profile_unlocked(void* const profile, void*, const unsigned long long* const flag) noexcept {
    return is_custom(flag) || g_profile_unlocked(profile, flag);
}

// The shop's unlock check (0x821B90) passes when the player's rank (profile
// +0x20, a u64; the profile is [[screen+0xB8]+0xC0]) reaches the button's
// required rank (holder +0x38, from RankToUnlock) and the profile's unlock map
// (profile +0xB0, keyed by type) marks the tower unlocked. The map cannot hold
// custom towers, so for them only the rank is compared.
bool __fastcall hooked_shop_unlocked(void* const screen, void*, void* const holder) noexcept {
    // The holder's first field is the definition; its type value is at +0xD0.
    if (holder != nullptr && screen != nullptr) {
        const auto definition = *static_cast<const std::uintptr_t*>(holder);
        if (definition != 0 && is_custom(reinterpret_cast<const unsigned long long*>(definition + 0xD0))) {
            const auto read = [](const std::uintptr_t address) {
                std::uintptr_t value{};
                std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
                return value;
            };
            const auto game = read(reinterpret_cast<std::uintptr_t>(screen) + 0xB8);
            const auto profile = game == 0 ? 0 : read(game + 0xC0);
            if (profile == 0) return true;
            unsigned long long rank{};
            std::memcpy(&rank, reinterpret_cast<const void*>(profile + 0x20), sizeof(rank));
            std::uint32_t required{};
            std::memcpy(&required, static_cast<const std::uint8_t*>(holder) + 0x38, sizeof(required));
            return rank >= required;
        }
    }
    return g_shop_unlocked(screen, holder);
}

// Availability is (locked | ~allowed) & flag in the mode's masks. Restricted
// masks never contain the custom identity bits, so asking about the custom
// flag itself fails in every restricted mode ("never"), and asking about the
// base flag follows the base tower ("base").
bool __fastcall hooked_shop_available(void* const screen, void*, const unsigned long long* const flag) noexcept {
    if (!is_custom(flag)) return g_shop_available(screen, flag);
    const CustomTower* const tower = g_registry->find_flag(*flag);
    if (tower == nullptr) return g_shop_available(screen, flag);
    switch (tower->spec.restricted_modes) {
    case RestrictedModes::always: return true;
    case RestrictedModes::never: return g_shop_available(screen, flag);
    case RestrictedModes::base: break;
    }
    return g_shop_available(screen, &tower->base_flag);
}

bool __fastcall hooked_upgrade_unlocked(void* const profile, void*, const unsigned long long* const flag,
    const unsigned path, const unsigned tier) noexcept {
    return is_custom(flag) || g_upgrade_unlocked(profile, flag, path, tier);
}

bool tracking_xp() { return g_xp_enabled && g_progress != nullptr && g_registry != nullptr; }

// Keeps a custom tower's node in the game's XP map at its true total. Called
// with g_registration_mutex held.
void track_xp(const CustomTower& tower, float* const node) {
    auto [entry, inserted] = g_xp_tracks.try_emplace(tower.index);
    XpTrack& track = entry->second;
    if (inserted) track.total = g_progress->xp(tower.id);
    if (node != track.node || *node < track.total) {
        // A new node only holds XP earned since it was created.
        *node += track.total;
        track.node = node;
    }
    track.total = *node;
}

void __fastcall hooked_profile_save(
    void* const profile, void*, void* const first, void* const second, void* const third) noexcept {
    // Take custom tower XP out of the game's map before it is saved (where it
    // would be written as "INVALID").
    // Custom nodes are written as "INVALID" entries, which reload onto key 0,
    // so they are emptied for the write and restored afterwards.
    std::vector<std::pair<float*, float>> hidden;
    if (tracking_xp() && g_xp_tracking_started && g_xp_profile != nullptr) {
        try {
            std::lock_guard lock(g_registration_mutex);
            for (const auto& tower : g_registry->towers()) {
                if (!tower->spec.xp_progression) continue;
                float* const node = g_tower_xp(g_xp_profile, &tower->flag);
                track_xp(*tower, node);
                g_progress->set_xp(tower->id, *node);
                hidden.emplace_back(node, *node);
                *node = 0.0F;
            }
        } catch (...) {
        }
    }
    {
        struct Depth final {
            Depth() noexcept { ++t_profile_save_depth; }
            ~Depth() { --t_profile_save_depth; }
        } depth;
        g_profile_save(profile, first, second, third);
    }
    for (const auto& [node, value] : hidden) *node = value;
    if (tracking_xp() && g_xp_tracking_started) {
        std::string error;
        if (!g_progress->save(error)) log_line("custom_tower_progress_unsaved " + error);
    }
}

template <typename T>
T read_at(const std::uintptr_t address) {
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}

// The round XP award (0x632B00) splits XP between tower types by pop count,
// but keys the profile XP map by only the low half of each type, which is
// zero for every custom value, so a custom tower's share lands on key 0 (the
// key vanilla agents with high-bit types also share). Move each custom
// tower's share of key 0's growth, by its pops among the zero-low-half towers
// the award counted, to the custom tower's own key. Offsets are the award's
// own: this+4/+8 tower list, tower +0x21C owner, +0x144 pops, and the profile
// at [[this+0x10]+0xC0], owner ID at [[[this+0x10]+0xC4]+0x144].
void __fastcall hooked_xp_award(void* const self, void*, const int round, const int amount) noexcept {
    if (!tracking_xp() || self == nullptr) {
        g_xp_award(self, round, amount);
        return;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(self);
    const auto game = read_at<std::uintptr_t>(base + 0x10);
    void* const profile = game == 0 ? nullptr : read_at<void*>(game + 0xC0);
    const unsigned long long zero_key = 0;
    float before = 0.0F;
    if (profile != nullptr) before = *g_tower_xp(profile, &zero_key);
    g_xp_award(self, round, amount);
    if (profile == nullptr) return;
    try {
        float* const zero = g_tower_xp(profile, &zero_key);
        const float delta = *zero - before;
        if (!(delta > 0.0F)) return;
        const auto owner_record = read_at<std::uintptr_t>(game + 0xC4);
        if (owner_record == 0) return;
        const int owner = read_at<int>(owner_record + 0x144);
        std::unordered_map<std::uint32_t, double> custom_pops;
        double zero_low_pops = 0.0;
        const auto first = read_at<std::uintptr_t>(base + 4);
        const auto last = read_at<std::uintptr_t>(base + 8);
        for (auto slot = first; slot < last; slot += sizeof(std::uintptr_t)) {
            const auto tower = read_at<std::uintptr_t>(slot);
            if (tower == 0 || read_at<int>(tower + 0x21C) != owner) continue;
            const auto type = read_at<unsigned long long>(tower + 0xD0);
            if (static_cast<std::uint32_t>(type) != 0) continue;
            const int pops = read_at<int>(tower + 0x144);
            if (pops <= 0) continue;
            zero_low_pops += pops;
            const CustomTower* const custom = g_registry->find_flag(type);
            if (custom != nullptr && custom->spec.xp_progression) custom_pops[custom->index] += pops;
        }
        if (!(zero_low_pops > 0.0) || custom_pops.empty()) return;
        std::lock_guard lock(g_registration_mutex);
        for (const auto& tower : g_registry->towers()) {
            const auto found = custom_pops.find(tower->index);
            if (found == custom_pops.end()) continue;
            const auto share = static_cast<float>(delta * (found->second / zero_low_pops));
            float* const node = g_tower_xp(profile, &tower->flag);
            track_xp(*tower, node);
            *node += share;
            track_xp(*tower, node);
            *zero -= share;
        }
    } catch (...) {
    }
}

// CLoc::GetText(out, const std::string& id, const SLanguage*) for custom tower IDs.
void* __fastcall hooked_text_lookup(void* const loc, void*, void* const out, const void* const key, void* const language) noexcept {
    const TowerRegistry* const registry = g_registry;
    if (registry != nullptr && key != nullptr) {
        try {
            const std::string id(read_string(key));
            if (const auto lookup = resolve_custom_text(id, *registry)) {
                {
                    std::lock_guard lock(g_generated_mutex);
                    if (g_logged_text.insert(id).second) {
                        log_line("custom_tower_text " + id + (lookup->text ? " mod" : " base"));
                    }
                }
                if (lookup->text) return g_construct(out, lookup->text->c_str());
                alignas(4) std::array<std::uint8_t, 0x18> base_key{};
                g_construct(base_key.data(), lookup->base_key.c_str());
                void* const result = g_text_lookup(loc, out, base_key.data(), language);
                g_destroy(base_key.data());
                return result;
            }
        } catch (...) {
        }
    }
    return g_text_lookup(loc, out, key, language);
}

float* __fastcall hooked_tower_xp(void* const profile, void*, const unsigned long long* const flag) noexcept {
    float* const node = g_tower_xp(profile, flag);
    if (!tracking_xp() || flag == nullptr) return node;
    const CustomTower* const tower = g_registry->find_flag(*flag);
    if (tower == nullptr || !tower->spec.xp_progression) return node;
    try {
        std::lock_guard lock(g_registration_mutex);
        if (g_registration_finished) {
            g_xp_profile = profile;
            if (!g_xp_tracking_started) {
                g_xp_tracking_started = true;
                log_line("custom_tower_xp_tracking towers=" + std::to_string(g_registry->size()));
            }
            track_xp(*tower, node);
        }
    } catch (...) {
    }
    return node;
}

}  // namespace

CustomTowerHooks::~CustomTowerHooks() { remove(); }

bool CustomTowerHooks::install(const CustomTowerTargets& targets, const TowerRegistry& registry,
    CustomTowerProgress& progress, Log log, std::string& error) {
    if (g_registry != nullptr) {
        error = "custom tower hooks are already installed";
        return false;
    }
    const std::array<void*, 14> required{targets.name_to_flag, targets.flag_to_name, targets.open_file,
        targets.construct_string, targets.destroy_string, targets.profile_tower_unlocked,
        targets.shop_tower_unlocked, targets.shop_tower_available, targets.profile_upgrade_unlocked,
        targets.profile_save, targets.profile_tower_xp, targets.text_lookup, targets.xp_award, targets.file_exists};
    for (void* const target : required) {
        if (target == nullptr) {
            error = "custom tower hook target is missing";
            return false;
        }
    }
    const MH_STATUS initialize_status = MH_Initialize();
    if (initialize_status != MH_OK && initialize_status != MH_ERROR_ALREADY_INITIALIZED) {
        error = "MinHook initialization failed";
        return false;
    }
    std::wstring executable(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) {
        error = "game directory is unavailable";
        return false;
    }
    executable.resize(length);
    g_game_directory = std::filesystem::path(executable).parent_path();
    char* diag = nullptr;
    std::size_t diag_size = 0;
    g_diag_open_files = _dupenv_s(&diag, &diag_size, "BTD5ML_DIAG_OPEN_FILES") == 0 && diag != nullptr;
    std::free(diag);
    g_diag_start = std::chrono::steady_clock::now();
    g_registry = &registry;
    g_progress = &progress;
    g_log = std::move(log);
    g_construct = reinterpret_cast<ConstructString>(targets.construct_string);
    g_destroy = reinterpret_cast<DestroyString>(targets.destroy_string);
    // The XP accessor and award are hooked later, only for XP progression.
    g_xp_accessor_target = targets.profile_tower_xp;
    g_xp_award_target = targets.xp_award;
    g_load_texture = reinterpret_cast<LoadTexture>(targets.texture_load);
    g_sfx_instance = reinterpret_cast<SfxInstance>(targets.sfx_instance);
    g_sfx_play = reinterpret_cast<SfxPlay>(targets.sfx_play);
    // Optional: mod sound files open through mmioOpenW (winmm), redirected by name.
    if (HMODULE winmm = GetModuleHandleW(L"winmm.dll"); winmm != nullptr && g_sfx_play != nullptr) {
        g_mmio_target = reinterpret_cast<void*>(GetProcAddress(winmm, "mmioOpenW"));
    }
    const std::array<std::pair<void*, std::pair<void*, void**>>, kXpHookSlot> hooks{{
        {targets.name_to_flag, {reinterpret_cast<void*>(&hooked_name_to_flag), reinterpret_cast<void**>(&g_name_to_flag)}},
        {targets.flag_to_name, {reinterpret_cast<void*>(&hooked_flag_to_name), reinterpret_cast<void**>(&g_flag_to_name)}},
        {targets.open_file, {reinterpret_cast<void*>(&hooked_open_file), reinterpret_cast<void**>(&g_open_file)}},
        {targets.profile_tower_unlocked, {reinterpret_cast<void*>(&hooked_profile_unlocked), reinterpret_cast<void**>(&g_profile_unlocked)}},
        {targets.shop_tower_unlocked, {reinterpret_cast<void*>(&hooked_shop_unlocked), reinterpret_cast<void**>(&g_shop_unlocked)}},
        {targets.shop_tower_available, {reinterpret_cast<void*>(&hooked_shop_available), reinterpret_cast<void**>(&g_shop_available)}},
        {targets.profile_upgrade_unlocked, {reinterpret_cast<void*>(&hooked_upgrade_unlocked), reinterpret_cast<void**>(&g_upgrade_unlocked)}},
        {targets.profile_save, {reinterpret_cast<void*>(&hooked_profile_save), reinterpret_cast<void**>(&g_profile_save)}},
        {targets.text_lookup, {reinterpret_cast<void*>(&hooked_text_lookup), reinterpret_cast<void**>(&g_text_lookup)}},
        {targets.file_exists, {reinterpret_cast<void*>(&hooked_file_exists), reinterpret_cast<void**>(&g_file_exists)}},
        // Optional: without it, mod sprite sheets are listed but not loaded.
        {targets.texture_get_sprite == nullptr ? nullptr : targets.texture_load_index,
            {reinterpret_cast<void*>(&hooked_load_sprite_index), reinterpret_cast<void**>(&g_load_sprite_index)}},
        {targets.texture_load_index == nullptr ? nullptr : targets.texture_get_sprite,
            {reinterpret_cast<void*>(&hooked_get_sprite), reinterpret_cast<void**>(&g_get_sprite)}},
        {g_mmio_target, {reinterpret_cast<void*>(&hooked_mmio_open), reinterpret_cast<void**>(&g_mmio_open)}},
    }};
    for (std::size_t i = 0; i < hooks.size(); ++i) {
        const auto& [target, detour] = hooks[i];
        if (target == nullptr) continue;
        if (MH_CreateHook(target, detour.first, detour.second) != MH_OK) {
            error = "custom tower hook " + std::to_string(i) + " creation failed";
            remove();
            return false;
        }
        g_installed[i] = target;
        if (MH_EnableHook(target) != MH_OK) {
            error = "custom tower hook " + std::to_string(i) + " enable failed";
            remove();
            return false;
        }
    }
    return true;
}

void CustomTowerHooks::remove() noexcept {
    for (void*& target : g_installed) {
        if (target == nullptr) continue;
        (void)MH_DisableHook(target);
        (void)MH_RemoveHook(target);
        target = nullptr;
    }
    g_registry = nullptr;
    g_progress = nullptr;
    g_xp_profile = nullptr;
    g_xp_tracks.clear();
    g_xp_tracking_started = false;
    g_xp_enabled = false;
    g_xp_accessor_target = nullptr;
    g_xp_award_target = nullptr;
    g_log = {};
    finish_registration();
}

std::string CustomTowerHooks::play_sound(const std::string_view id) noexcept {
    try {
        const TowerRegistry* const registry = g_registry;
        if (registry == nullptr || g_sfx_instance == nullptr || g_sfx_play == nullptr) {
            return "sounds are unavailable on this game build";
        }
        const auto& sounds = registry->sounds();
        const auto found = std::find_if(sounds.begin(), sounds.end(), [id](const auto& sound) { return sound->id == id; });
        if (found == sounds.end()) return "unknown sound '" + std::string(id) + "'";
        void* const manager = g_sfx_instance();
        if (manager == nullptr) return "the sound manager is not ready";
        // Effects are 56-byte entries in a vector at +0x10/+0x14; mod effects
        // are appended last to game_sfx.json, so they are the last entries.
        std::uintptr_t first{};
        std::uintptr_t last{};
        std::memcpy(&first, static_cast<const std::uint8_t*>(manager) + 0x10, sizeof(first));
        std::memcpy(&last, static_cast<const std::uint8_t*>(manager) + 0x14, sizeof(last));
        constexpr std::uintptr_t kEffectSize = 56;
        const std::size_t count = last >= first ? (last - first) / kEffectSize : 0;
        if (count < sounds.size()) return "match sounds are not loaded yet";
        const auto position = static_cast<std::size_t>(found - sounds.begin());
        const std::size_t index = count - sounds.size() + position;
        // Logged once per sound, to show where mod effects landed.
        static std::mutex logged_mutex;
        static std::unordered_map<std::string, bool> logged;
        {
            std::lock_guard lock(logged_mutex);
            if (!logged[std::string(id)]) {
                logged[std::string(id)] = true;
                log_line("custom_sound_play id=" + std::string(id) + ";index=" + std::to_string(index) +
                         ";effects=" + std::to_string(count));
            }
        }
        g_sfx_play(manager, static_cast<int>(index));
        return {};
    } catch (...) {
        return "the sound could not be played";
    }
}

void CustomTowerHooks::finish_registration() noexcept {
    bool wants_xp = false;
    {
        std::lock_guard lock(g_registration_mutex);
        if (g_registration_finished) return;
        g_registration_finished = true;
        if (g_registry != nullptr && g_xp_accessor_target != nullptr && g_xp_award_target != nullptr) {
            for (const auto& tower : g_registry->towers()) wants_xp = wants_xp || tower->spec.xp_progression;
        }
    }
    if (wants_xp) {
        // Slots after the core hooks; remove() takes them down with the rest.
        const std::array<std::pair<void*, std::pair<void*, void**>>, 2> xp_hooks{{
            {g_xp_accessor_target, {reinterpret_cast<void*>(&hooked_tower_xp), reinterpret_cast<void**>(&g_tower_xp)}},
            {g_xp_award_target, {reinterpret_cast<void*>(&hooked_xp_award), reinterpret_cast<void**>(&g_xp_award)}},
        }};
        bool installed = true;
        for (std::size_t i = 0; i < xp_hooks.size(); ++i) {
            const auto& [target, detour] = xp_hooks[i];
            if (MH_CreateHook(target, detour.first, detour.second) != MH_OK) {
                installed = false;
                break;
            }
            g_installed[kXpHookSlot + i] = target;
            if (MH_EnableHook(target) != MH_OK) {
                installed = false;
                break;
            }
        }
        if (installed) {
            g_xp_enabled = true;
            log_line("custom_tower_xp_hooks_installed");
        } else {
            for (std::size_t i = kXpHookSlot; i < g_installed.size(); ++i) {
                if (g_installed[i] == nullptr) continue;
                (void)MH_DisableHook(g_installed[i]);
                (void)MH_RemoveHook(g_installed[i]);
                g_installed[i] = nullptr;
            }
            log_line("custom_tower_xp_hooks_failed");
        }
    }
    g_registration_done.notify_all();
}

}  // namespace btd5loader::runtime
