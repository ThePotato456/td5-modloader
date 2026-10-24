// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace btd5loader::runtime {

class CustomTowerProgress;
class TowerRegistry;

// Resolved addresses of the custom_tower.* symbols.
struct CustomTowerTargets final {
    void* name_to_flag{};             // CFlagStringConvertor name -> flag
    void* flag_to_name{};             // CFlagStringConvertor flag -> name
    void* open_file{};                // CBaseFileIO::OpenFile
    void* construct_string{};         // game std::string from a C string
    void* destroy_string{};           // game std::string destructor
    void* profile_tower_unlocked{};   // profile unlocked-towers lookup by flag
    void* shop_tower_unlocked{};      // shop button rank/unlock check
    void* shop_tower_available{};     // shop button level-mask availability check
    void* profile_upgrade_unlocked{}; // profile upgrade-unlock lookup by flag
    void* profile_save{};             // profile writer (Profile.save)
    void* profile_tower_xp{};         // per-type XP accessor (returns float*)
    void* text_lookup{};              // CLoc::GetText by string ID
    void* xp_award{};                 // round XP award (0x632B00)
    void* file_exists{};              // CBaseFileIO file-exists check (0x994170)
    void* texture_load_index{};       // texture manager: read a sprite-sheet index (0x99F4A0); optional
    void* texture_get_sprite{};       // texture manager: sprite by sheet and cell (0x9A3400); optional
    void* texture_load{};             // texture manager: load a sheet's image by name (0x9A3C00); optional
    void* sfx_instance{};             // C_GameSFXManager singleton accessor (0x628BE0); optional
    void* sfx_play{};                 // C_GameSFXManager::Play(int) (0x628D30); optional
};

// Native hooks that give registered custom towers their encoded identity.
// The tower name <-> flag converter (category 0) resolves custom native names
// and values through the registry; the profile and shop unlock checks pass
// registered custom towers (their own unlock state belongs in the mod-owned
// store, which does not exist yet); and CBaseFileIO::OpenFile serves custom
// tower files and the merged tower cache list and shop order, each built from
// vanilla content read through the game's own IFile and written under
// kGeneratedAssetRoot in the game directory. While the profile writer runs,
// custom towers are named "INVALID" (an entry vanilla saves already carry),
// so no custom identifier reaches vanilla progression data. Towers that opt
// into XP progression keep their XP in CustomTowerProgress: two extra hooks
// (the per-type XP accessor and the round XP award) are installed when
// registration finishes, only if any registered tower opted in. Text lookups for a custom
// tower's string IDs return the mod's name and description, or the base
// tower's text. Requests for the shared files (cache list, shop order) wait
// until mod loading has finished registering towers. The round XP award
// keys the profile XP map by only the low half of each tower type, which is
// zero for every custom value, so after each award the custom towers' share
// of key 0's growth is moved to their own keys.
class CustomTowerHooks final {
public:
    using Log = std::function<void(std::string_view)>;

    CustomTowerHooks() = default;
    ~CustomTowerHooks();
    CustomTowerHooks(const CustomTowerHooks&) = delete;
    CustomTowerHooks& operator=(const CustomTowerHooks&) = delete;

    // The registry and progress must outlive the hooks; towers may be
    // registered after installation, until finish_registration().
    [[nodiscard]] bool install(const CustomTowerTargets& targets, const TowerRegistry& registry,
        CustomTowerProgress& progress, Log log, std::string& error);
    void remove() noexcept;

    // Ends registration, installs the XP hooks if any registered tower uses
    // XP progression, and releases game threads waiting for shared files.
    static void finish_registration() noexcept;

    // Plays a registered mod sound in the current match. Returns an empty
    // string, or why it could not play (unknown sound, sounds not loaded yet,
    // or the game functions are unavailable).
    [[nodiscard]] static std::string play_sound(std::string_view id) noexcept;
};

}  // namespace btd5loader::runtime
