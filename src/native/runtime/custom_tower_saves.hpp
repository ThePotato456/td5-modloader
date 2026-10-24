// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace btd5loader::runtime {

class TowerRegistry;

// Match saves (Game.save, Odyssey.save, Bloonvasion.save) store each placed
// tower by native name. A save that names a custom tower no enabled mod
// registers would rebuild that tower as type 0, so before the game can read
// it the loader moves it under its own storage ("held") and reports which
// towers are missing; once every named tower is registered again, it moves
// the save back. Nothing in a save is rewritten.
//
// Native names are hashes of the tower ID, so the loader also keeps a map of
// every native name it has registered to its public ID, to report a missing
// tower by "<modId>:<towerId>".

// Decodes a DGDATA save (14-byte "DGDATA" + 8 hex header, then each byte
// offset by 21 + i % 6) to its JSON text.
[[nodiscard]] std::optional<std::string> decode_dgdata(std::string_view bytes);

// Custom tower native names ("CT" + 16 upper-case hex) in a decoded save,
// sorted and unique.
[[nodiscard]] std::vector<std::string> custom_tower_names_in_save(std::string_view json);

// Holds or restores the match saves in `docs` (the game's Data/Docs folder),
// using `store` (the loader's custom tower storage) for held saves and the
// name map. Returns one log line per action taken.
[[nodiscard]] std::vector<std::string> guard_match_saves(
    const std::filesystem::path& docs, const std::filesystem::path& store, const TowerRegistry& registry);

}  // namespace btd5loader::runtime
