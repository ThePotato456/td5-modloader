// SPDX-License-Identifier: GPL-3.0-only
#include "custom_tower_saves.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <sstream>

#include <nlohmann/json.hpp>

#include "tower_registry.hpp"

namespace btd5loader::runtime {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t kHeaderSize = 14;
constexpr std::size_t kNativeNameHexDigits = 16;
constexpr std::array<const wchar_t*, 3> kMatchSaves{L"Game.save", L"Odyssey.save", L"Bloonvasion.save"};

bool is_upper_hex(const char value) { return (value >= '0' && value <= '9') || (value >= 'A' && value <= 'F'); }

std::optional<std::string> read_file(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::nullopt;
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

using NameMap = std::map<std::string, std::string, std::less<>>;

// native name -> public ID for every custom tower the loader has registered.
NameMap load_names(const fs::path& path) {
    NameMap names;
    const auto text = read_file(path);
    if (!text) return names;
    try {
        const auto json = nlohmann::json::parse(*text);
        for (const auto& [native, id] : json.at("names").items()) {
            if (id.is_string()) names.emplace(native, id.get<std::string>());
        }
    } catch (const std::exception&) {
        names.clear();
    }
    return names;
}

void save_names(const fs::path& path, const NameMap& names) {
    nlohmann::json map = nlohmann::json::object();
    for (const auto& [native, id] : names) map[native] = id;
    std::error_code ignored;
    fs::create_directories(path.parent_path(), ignored);
    const auto temporary = fs::path(path).concat(L".tmp");
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream << nlohmann::json{{"version", 1}, {"names", std::move(map)}}.dump(2);
        if (!stream) return;
    }
    fs::rename(temporary, path, ignored);
}

bool move_file(const fs::path& from, const fs::path& to) {
    std::error_code error;
    fs::create_directories(to.parent_path(), error);
    fs::rename(from, to, error);
    if (!error) return true;
    // Different volumes: copy, then remove the source only once the copy exists.
    if (!fs::copy_file(from, to, fs::copy_options::overwrite_existing, error) || error) return false;
    fs::remove(from, error);
    return !error;
}

// Names in a save that no registered tower has, as public IDs where known.
std::vector<std::string> missing_towers(const fs::path& save, const TowerRegistry& registry, const NameMap& names,
    bool& readable) {
    readable = false;
    std::vector<std::string> missing;
    const auto bytes = read_file(save);
    if (!bytes) return missing;
    const auto json = decode_dgdata(*bytes);
    if (!json) return missing;
    readable = true;
    for (const auto& native : custom_tower_names_in_save(*json)) {
        if (registry.find_native_name(native) != nullptr) continue;
        const auto known = names.find(native);
        missing.push_back(known == names.end() ? native : known->second);
    }
    return missing;
}

std::string join(const std::vector<std::string>& values) {
    std::string out;
    for (const auto& value : values) out += (out.empty() ? "" : ",") + value;
    return out;
}

}  // namespace

std::optional<std::string> decode_dgdata(const std::string_view bytes) {
    if (bytes.size() < kHeaderSize || bytes.substr(0, 6) != "DGDATA") return std::nullopt;
    std::string text;
    text.reserve(bytes.size() - kHeaderSize);
    for (std::size_t i = kHeaderSize; i < bytes.size(); ++i) {
        const auto offset = static_cast<unsigned char>(21 + (i - kHeaderSize) % 6);
        text.push_back(static_cast<char>(static_cast<unsigned char>(bytes[i]) - offset));
    }
    return text;
}

std::vector<std::string> custom_tower_names_in_save(const std::string_view json) {
    std::vector<std::string> names;
    constexpr std::size_t length = 2 + kNativeNameHexDigits;
    for (std::size_t at = json.find("\"CT"); at != std::string_view::npos; at = json.find("\"CT", at + 1)) {
        if (at + 1 + length >= json.size() || json[at + 1 + length] != '"') continue;
        const auto name = json.substr(at + 1, length);
        if (std::all_of(name.begin() + 2, name.end(), is_upper_hex)) names.emplace_back(name);
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

std::vector<std::string> guard_match_saves(const fs::path& docs, const fs::path& store, const TowerRegistry& registry) {
    std::vector<std::string> lines;
    const auto names_path = store / L"names.json";
    NameMap names = load_names(names_path);
    bool names_changed = false;
    for (const auto& tower : registry.towers()) {
        auto& id = names[tower->native_name];
        if (id != tower->id) {
            id = tower->id;
            names_changed = true;
        }
    }
    if (names_changed) save_names(names_path, names);

    const auto held_root = store / L"held-saves";
    std::error_code ignored;
    for (const wchar_t* const file : kMatchSaves) {
        const auto live = docs / file;
        const auto held = held_root / file;
        const std::string label = fs::path(file).string();
        bool readable = false;
        if (fs::exists(live, ignored)) {
            const auto missing = missing_towers(live, registry, names, readable);
            if (missing.empty()) continue;
            // Keep an older held save rather than overwrite it.
            if (fs::exists(held, ignored)) {
                for (int n = 1;; ++n) {
                    const auto older = fs::path(held).concat(L"." + std::to_wstring(n));
                    if (fs::exists(older, ignored)) continue;
                    (void)move_file(held, older);
                    break;
                }
            }
            if (move_file(live, held)) {
                lines.push_back("custom_tower_save_held file=" + label + ";missing=" + join(missing));
            } else {
                lines.push_back("custom_tower_save_hold_failed file=" + label + ";missing=" + join(missing));
            }
        } else if (fs::exists(held, ignored)) {
            const auto missing = missing_towers(held, registry, names, readable);
            if (!readable) continue;
            if (!missing.empty()) {
                lines.push_back("custom_tower_save_still_held file=" + label + ";missing=" + join(missing));
            } else if (move_file(held, live)) {
                lines.push_back("custom_tower_save_restored file=" + label);
            }
        }
    }
    return lines;
}

}  // namespace btd5loader::runtime
