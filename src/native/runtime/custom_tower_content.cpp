// SPDX-License-Identifier: GPL-3.0-only
#include "custom_tower_content.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string_view>

namespace btd5loader::runtime {
namespace {

using Json = nlohmann::ordered_json;

constexpr std::size_t kMaximumDepth = 16;
constexpr std::size_t kMaximumWeaponBytes = 64 * 1024;
constexpr std::size_t kMaximumNameLength = 64;

// The game's task, movement, damage and collision type names (from its type
// tables), and the tower targeting modes a player can pick.
constexpr std::array<std::string_view, 34> kTaskTypes{"Projectile", "MultiFire", "Damage", "StatusEffect",
    "RemoveStatusEffect", "AreaOfEffect", "RandomFire", "TimerFire", "Effect", "TextEffect", "BloonSpawnedEvent",
    "RayIntersect", "ChainTasks", "Collectable", "LaunchAircraft", "TowerModifier", "Harpoon", "Sacrifice",
    "AddChildSprite", "ChangeTerrain", "CreateTower", "ResetPopCount", "EnableNextWeaponSlot", "EnableWeaponSlot",
    "ResetCooldown", "RedeployTower", "ChangeSubtaskEnabled", "CollectCollectables", "RestoreAmmo",
    "FireTaskAtLocation", "OverclockTask", "RBEProjectile", "TrapBloon", "DamageSpread"};
constexpr std::array<std::string_view, 9> kMovementTypes{"Forward", "RotateAroundTower", "GoToTarget", "Target",
    "MotionCurve", "ReturnToSender", "MoveToTouch", "BeeMovement", "BeeSwarmMovement"};
constexpr std::array<std::string_view, 4> kDamageTypes{"Piercing", "Fire", "Explosive", "Plasma"};
constexpr std::array<std::string_view, 3> kCollisionTypes{"Once", "Continual", "None"};
constexpr std::array<std::string_view, 4> kTargetingModes{"First", "Last", "Strong", "Close"};

template <std::size_t N>
bool one_of(const std::array<std::string_view, N>& values, const std::string_view value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

template <std::size_t N>
std::string list(const std::array<std::string_view, N>& values) {
    std::string out;
    for (const auto value : values) out += (out.empty() ? "" : ", ") + std::string(value);
    return out;
}

bool valid_text(const std::string& text, const std::size_t maximum) {
    return text.size() <= maximum &&
           std::none_of(text.begin(), text.end(), [](const unsigned char c) { return c < 0x20 || c == 0x7F; });
}

std::string number(const Json& json, const char* key, const double low, const double high, std::optional<double>& out) {
    if (!json.contains(key)) return {};
    const auto& value = json.at(key);
    if (!value.is_number() || !std::isfinite(value.get<double>()) || value.get<double>() < low ||
        value.get<double>() > high) {
        return std::string(key) + " must be a number from " + std::to_string(low) + " to " + std::to_string(high);
    }
    out = value.get<double>();
    return {};
}

std::string integer(const Json& json, const char* key, const std::int64_t low, const std::int64_t high,
    std::optional<std::int64_t>& out) {
    if (!json.contains(key)) return {};
    const auto& value = json.at(key);
    if (!value.is_number_integer() || value.get<std::int64_t>() < low || value.get<std::int64_t>() > high) {
        return std::string(key) + " must be an integer from " + std::to_string(low) + " to " + std::to_string(high);
    }
    out = value.get<std::int64_t>();
    return {};
}

std::string unknown_keys(const Json& json, const std::set<std::string_view>& allowed, const std::string& where) {
    for (const auto& [key, value] : json.items()) {
        if (!allowed.contains(key)) return where + " has unknown field '" + key + "'";
    }
    return {};
}

std::size_t depth(const Json& json) {
    std::size_t deepest = 0;
    if (json.is_structured()) {
        for (const auto& child : json) deepest = (std::max)(deepest, depth(child));
        return deepest + 1;
    }
    return 0;
}

// Checks a Tasks array: every task names a known task type, and nested
// movement, damage and collision types are known too.
std::string check_tasks(const Json& tasks, const std::string& where) {
    if (!tasks.is_array()) return where + " Tasks must be a list";
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        const auto& task = tasks[i];
        const std::string at = where + " task " + std::to_string(i + 1);
        if (!task.is_object()) return at + " must be a table";
        const auto type = task.value("Type", std::string());
        if (!one_of(kTaskTypes, type)) return at + " has unknown Type '" + type + "' (known: " + list(kTaskTypes) + ")";
        if (task.contains("DamageType") &&
            (!task["DamageType"].is_string() || !one_of(kDamageTypes, task["DamageType"].get<std::string>()))) {
            return at + " DamageType must be one of " + list(kDamageTypes);
        }
        if (task.contains("CollisionType") &&
            (!task["CollisionType"].is_string() || !one_of(kCollisionTypes, task["CollisionType"].get<std::string>()))) {
            return at + " CollisionType must be one of " + list(kCollisionTypes);
        }
        if (task.contains("Movement")) {
            const auto& movement = task["Movement"];
            if (!movement.is_object() || !one_of(kMovementTypes, movement.value("Type", std::string()))) {
                return at + " Movement Type must be one of " + list(kMovementTypes);
            }
        }
        if (task.contains("Tasks")) {
            if (auto error = check_tasks(task["Tasks"], at); !error.empty()) return error;
        }
    }
    return {};
}

std::string check_weapon(const Json& weapon, const std::string& where) {
    if (!weapon.is_object()) return where + " must be a table";
    if (depth(weapon) > kMaximumDepth) return where + " is nested too deeply";
    if (weapon.dump().size() > kMaximumWeaponBytes) return where + " is too large";
    if (!weapon.contains("Type") || !weapon["Type"].is_string() || weapon["Type"].get<std::string>().empty()) {
        return where + " needs a Type";
    }
    for (const char* key : {"TargetRange", "CooldownTime"}) {
        if (!weapon.contains(key) || !weapon[key].is_number() || !(weapon[key].get<double>() > 0)) {
            return where + " needs a positive " + key;
        }
    }
    if (!weapon.contains("Tasks") || !weapon["Tasks"].is_array() || weapon["Tasks"].empty()) {
        return where + " needs at least one task";
    }
    return check_tasks(weapon["Tasks"], where);
}

// Depth-first search for the first task of `type` in a Tasks tree.
Json* first_task(Json& tasks, const std::string_view type) {
    if (!tasks.is_array()) return nullptr;
    for (auto& task : tasks) {
        if (!task.is_object()) continue;
        if (task.value("Type", std::string()) == type) return &task;
        if (task.contains("Tasks")) {
            if (Json* found = first_task(task["Tasks"], type)) return found;
        }
    }
    return nullptr;
}

bool valid_art_name(const std::string& name) {
    return !name.empty() && name.size() <= kMaximumNameLength &&
           std::all_of(name.begin(), name.end(), [](const unsigned char c) {
               return std::isalnum(c) != 0 || c == '_' || c == '-';
           });
}

std::string art_field(const Json& json, const char* key, std::string& out) {
    if (!json.contains(key)) return {};
    if (!json[key].is_string() || !valid_art_name(json[key].get<std::string>())) {
        return std::string(key) + " must be a sprite cell name (letters, digits, '_' and '-')";
    }
    out = json[key].get<std::string>();
    return {};
}

Json& upgrade_path_entry(Json& definition, const char* key, const std::size_t path) {
    auto& paths = definition[key];
    if (!paths.is_array()) paths = Json::array();
    while (paths.size() <= path) paths.push_back(Json::array());
    return paths[path];
}

}  // namespace

std::string custom_weapon_file(const std::size_t index) { return "btd5ml_w" + std::to_string(index) + ".weapon"; }

std::string custom_upgrade_weapon_file(const std::size_t path, const std::size_t tier) {
    return std::string("btd5ml_u") + (path == 0 ? "a" : "b") + std::to_string(tier) + ".weapon";
}

std::string stats_weapon_file(const std::string_view base_file) { return "btd5ml_p_" + std::string(base_file); }

std::string parse_tower_stats(const Json& json, CustomTowerStats& stats) {
    if (!json.is_object()) return "stats must be a table";
    if (auto error = unknown_keys(json, {"range", "cooldown", "pierce", "damage", "projectile_speed", "footprint",
                                            "camo", "targeting", "placement"},
            "stats");
        !error.empty()) {
        return error;
    }
    for (auto error : {number(json, "range", 1, 2000, stats.range), number(json, "cooldown", 0.01, 60, stats.cooldown),
             integer(json, "pierce", 1, 1000, stats.pierce), integer(json, "damage", 0, 1000, stats.damage),
             number(json, "projectile_speed", 1, 10000, stats.projectile_speed),
             number(json, "footprint", 1, 200, stats.footprint)}) {
        if (!error.empty()) return "stats " + error;
    }
    if (json.contains("camo")) {
        if (!json["camo"].is_boolean()) return "stats camo must be true or false";
        stats.camo = json["camo"].get<bool>();
    }
    if (json.contains("targeting")) {
        const auto& value = json["targeting"];
        if (!value.is_string() || !one_of(kTargetingModes, value.get<std::string>())) {
            return "stats targeting must be one of " + list(kTargetingModes);
        }
        stats.targeting = value.get<std::string>();
    }
    if (json.contains("placement")) {
        const auto& value = json["placement"];
        if (!value.is_string() || (value != "land" && value != "water" && value != "both")) {
            return "stats placement must be \"land\", \"water\" or \"both\"";
        }
        stats.placement = value.get<std::string>();
    }
    return {};
}

std::string parse_tower_weapon(const Json& json, Json& weapon) {
    if (!json.is_object()) return "weapon must be a table";
    weapon = json;
    if (!weapon.contains("Type")) weapon["Type"] = "DartMonkey";
    if (!weapon.contains("FireDelayTime")) weapon["FireDelayTime"] = 0.0;
    return check_weapon(weapon, "weapon");
}

std::string parse_upgrade_tier(const Json& json, CustomUpgrade& tier) {
    if (!json.is_object()) return "upgrade tier must be a table";
    if (auto error = unknown_keys(json, {"name", "cost", "description", "range", "cooldown", "pierce", "camo", "weapon",
                                            "tower_upgrade", "weapon_upgrade", "icon", "avatar", "sprite"},
            "upgrade tier");
        !error.empty()) {
        return error;
    }
    if (!json.contains("name") || !json["name"].is_string()) return "upgrade tier needs a name";
    tier.name = json["name"].get<std::string>();
    std::optional<std::int64_t> cost;
    if (auto error = integer(json, "cost", 0, 1'000'000, cost); !error.empty()) return "upgrade tier " + error;
    if (!cost) return "upgrade '" + tier.name + "' needs a cost";
    tier.cost = *cost;
    if (json.contains("description")) {
        if (!json["description"].is_string()) return "upgrade '" + tier.name + "' description must be text";
        tier.description = json["description"].get<std::string>();
    }
    Json effect = Json::object();
    if (json.contains("tower_upgrade")) {
        if (!json["tower_upgrade"].is_object()) return "upgrade '" + tier.name + "' tower_upgrade must be a table";
        effect["TowerUpgrade"] = json["tower_upgrade"];
    }
    if (json.contains("weapon_upgrade")) {
        if (!json["weapon_upgrade"].is_object()) return "upgrade '" + tier.name + "' weapon_upgrade must be a table";
        effect["WeaponUpgrade"] = json["weapon_upgrade"];
    }
    std::optional<double> range;
    std::optional<double> cooldown;
    std::optional<std::int64_t> pierce;
    for (auto error : {number(json, "range", -2000, 2000, range), number(json, "cooldown", -60, 60, cooldown),
             integer(json, "pierce", -1000, 1000, pierce)}) {
        if (!error.empty()) return "upgrade '" + tier.name + "' " + error;
    }
    if (range || cooldown || pierce) {
        auto& weapon_upgrade = effect["WeaponUpgrade"];
        if (!weapon_upgrade.contains("SlotIndices")) weapon_upgrade["SlotIndices"] = Json::array({0});
        if (range) weapon_upgrade["Adjust"]["TargetRange"] = *range;
        if (cooldown) weapon_upgrade["Adjust"]["CooldownTime"] = *cooldown;
        if (pierce) {
            auto& tasks = weapon_upgrade["TaskUpgrade"];
            if (!tasks.is_array()) tasks = Json::array();
            tasks.push_back({{"Operation", "SelectByType"}, {"Type", "Projectile"}});
            tasks.push_back({{"Operation", "Adjust"}, {"NumPersists", *pierce}});
        }
    }
    if (json.contains("camo")) {
        if (!json["camo"].is_boolean()) return "upgrade '" + tier.name + "' camo must be true or false";
        effect["TowerUpgrade"]["Replace"]["CanTargetCamo"] = json["camo"].get<bool>();
    }
    if (json.contains("weapon")) {
        Json weapon;
        if (auto error = parse_tower_weapon(json["weapon"], weapon); !error.empty()) {
            return "upgrade '" + tier.name + "' " + error;
        }
        tier.weapon = std::move(weapon);
    }
    for (auto [key, out] : {std::pair<const char*, std::string*>{"icon", &tier.icon}, {"avatar", &tier.avatar}}) {
        if (auto error = art_field(json, key, *out); !error.empty()) return "upgrade '" + tier.name + "' " + error;
    }
    if (json.contains("sprite")) {
        if (auto error = parse_sprite(json["sprite"], tier.sprite, tier.sprite_frames, tier.sprite_length);
            !error.empty()) {
            return "upgrade '" + tier.name + "' " + error;
        }
    }
    tier.effect = std::move(effect);
    return {};
}

std::string parse_sprite(const Json& json, std::string& sprite, std::vector<std::string>& frames, double& length) {
    constexpr std::size_t kMaximumFrames = 16;
    frames.clear();
    if (json.is_string()) {
        if (!valid_art_name(json.get<std::string>())) return "sprite must be a sprite cell name";
        sprite = json.get<std::string>();
        return {};
    }
    if (!json.is_object()) return "sprite must be a cell name or { frames = {...}, length = seconds }";
    if (auto error = unknown_keys(json, {"frames", "length"}, "sprite"); !error.empty()) return error;
    if (!json.contains("frames") || !json["frames"].is_array() || json["frames"].empty() ||
        json["frames"].size() > kMaximumFrames) {
        return "sprite frames must list 1 to 16 cell names";
    }
    for (const auto& frame : json["frames"]) {
        if (!frame.is_string() || !valid_art_name(frame.get<std::string>())) return "sprite frames must be cell names";
        frames.push_back(frame.get<std::string>());
    }
    std::optional<double> seconds;
    if (auto error = number(json, "length", 0.01, 10, seconds); !error.empty()) return "sprite " + error;
    length = seconds.value_or(0.2);
    sprite = frames.front();
    if (frames.size() == 1) frames.clear();
    return {};
}

std::string load_custom_texture(const std::filesystem::path& directory, const std::string& name, CustomTexture& texture) {
    constexpr std::uintmax_t kMaximumImageBytes = 16U * 1024U * 1024U;
    if (!valid_art_name(name)) return "texture name '" + name + "' must use letters, digits, '_' and '-'";
    texture.name = name;
    texture.image = directory / (name + ".png");
    texture.atlas = directory / (name + ".xml");
    std::error_code ignored;
    for (const auto& file : {texture.image, texture.atlas}) {
        if (!std::filesystem::is_regular_file(file, ignored)) {
            return "texture '" + name + "' is missing " + file.filename().string();
        }
    }
    if (std::filesystem::file_size(texture.image, ignored) > kMaximumImageBytes) {
        return "texture '" + name + "' image is larger than 16 MB";
    }
    const auto tablet = directory / "Tablet";
    if (std::filesystem::is_regular_file(tablet / (name + ".png"), ignored) &&
        std::filesystem::is_regular_file(tablet / (name + ".xml"), ignored)) {
        texture.tablet_image = tablet / (name + ".png");
        texture.tablet_atlas = tablet / (name + ".xml");
    }
    std::ifstream stream(texture.atlas, std::ios::binary);
    std::stringstream buffer;
    buffer << stream.rdbuf();
    const std::string atlas = buffer.str();
    std::smatch frame;
    if (!std::regex_search(atlas, frame, std::regex(R"re(<FrameInformation\s[^>]*name\s*=\s*"([^"]*)")re")) ||
        frame[1].str() != name) {
        return "texture '" + name + "' atlas must have <FrameInformation name=\"" + name + "\" ...>";
    }
    texture.cells.clear();
    const std::regex cell(R"re(<Cell\s[^>]*name\s*=\s*"([^"]*)")re");
    for (auto it = std::sregex_iterator(atlas.begin(), atlas.end(), cell); it != std::sregex_iterator(); ++it) {
        texture.cells.push_back((*it)[1].str());
    }
    if (texture.cells.empty()) return "texture '" + name + "' atlas has no cells";
    return {};
}

namespace {

void collect_weapon_art(const Json& json, const std::string& where, std::vector<ArtReference>& out) {
    if (json.is_object()) {
        for (const auto& [key, value] : json.items()) {
            if (value.is_string() && !value.get<std::string>().empty()) {
                if (key == "GraphicName") out.push_back({false, value.get<std::string>(), where});
                if (key == "SpriteFile") out.push_back({true, value.get<std::string>(), where});
            }
            collect_weapon_art(value, where, out);
        }
    } else if (json.is_array()) {
        for (const auto& value : json) collect_weapon_art(value, where, out);
    }
}

}  // namespace

std::vector<ArtReference> collect_art_references(const CustomTowerSpec& spec) {
    std::vector<ArtReference> out;
    for (const auto& [name, where] : {std::pair{spec.icon, "icon"}, {spec.avatar, "avatar"}}) {
        if (!name.empty()) out.push_back({false, name, where});
    }
    for (std::size_t i = 0; i < spec.weapons.size(); ++i) {
        collect_weapon_art(spec.weapons[i], "weapon " + std::to_string(i + 1), out);
    }
    for (const auto& path : spec.upgrades) {
        for (const auto& tier : path) {
            const std::string where = "upgrade '" + tier.name + "'";
            if (!tier.icon.empty()) out.push_back({false, tier.icon, where + " icon"});
            if (!tier.avatar.empty()) out.push_back({false, tier.avatar, where + " avatar"});
            collect_weapon_art(tier.effect, where, out);
            if (tier.weapon) collect_weapon_art(*tier.weapon, where + " weapon", out);
        }
    }
    return out;
}

std::vector<std::string> custom_sprite_cells(const CustomTowerSpec& spec) {
    std::vector<std::string> cells;
    if (spec.sprite.empty()) return cells;
    cells.push_back(spec.sprite);
    for (const auto& path : spec.upgrades) {
        for (const auto& tier : path) {
            if (!tier.sprite.empty() && std::find(cells.begin(), cells.end(), tier.sprite) == cells.end()) {
                cells.push_back(tier.sprite);
            }
        }
    }
    return cells;
}

std::string custom_sprite_file(const CustomTower& tower, const std::size_t index) {
    return "btd5ml_" + tower.native_name + "_" + std::to_string(index) + ".json";
}

Json build_tower_sprite(const CustomTower& tower, const std::size_t index) {
    const auto& spec = tower.spec;
    const auto cells = custom_sprite_cells(spec);
    const std::string& cell = cells.at(index);
    // The frames and length of whichever tower or tier sprite starts with this cell.
    std::vector<std::string> frames{cell};
    double length = 0.2;
    const auto take = [&](const std::string& sprite, const std::vector<std::string>& animated, const double seconds) {
        if (sprite == cell && !animated.empty()) {
            frames = animated;
            length = seconds;
        }
    };
    take(spec.sprite, spec.sprite_frames, spec.sprite_length);
    for (const auto& path : spec.upgrades) {
        for (const auto& tier : path) take(tier.sprite, tier.sprite_frames, tier.sprite_length);
    }
    const auto sheet_of = [&spec](const std::string& frame) {
        for (const auto& texture : spec.textures) {
            if (std::find(texture.cells.begin(), texture.cells.end(), frame) != texture.cells.end()) return texture.name;
        }
        return std::string();
    };
    const auto pose = [](const bool shown) {
        return Json{{"Alignment", {0, 0}}, {"Alpha", 1.0}, {"Angle", 0.0}, {"Flip", 0}, {"Position", {0.0, 0.0}},
            {"Scale", {1.0, 1.0}}, {"Shown", shown}};
    };
    // The shape of vanilla sprites (TowerSprites/SunGod.json for one cell,
    // DartMonkey.json for frames shown in turn while the tower fires).
    Json actors = Json::array();
    Json sprite_info = Json::array();
    Json timelines = Json::array();
    const double step = length / static_cast<double>(frames.size());
    for (std::size_t i = 0; i < frames.size(); ++i) {
        Json actor = pose(i == 0);
        actor["Colour"] = 4294967295U;
        actor["sprite"] = frames[i];
        actor["type"] = 1;
        actor["uid"] = i + 1;
        actors.push_back(std::move(actor));
        sprite_info.push_back(Json{{"SpriteInfo", frames[i]}, {"Texture", sheet_of(frames[i])}});
        if (frames.size() == 1) {
            timelines.push_back(Json{{"spriteuid", 1}, {"stage", nullptr}});
            continue;
        }
        Json stage = Json::array();
        const auto key = [&pose](const double time, const bool shown) {
            Json frame = pose(shown);
            frame["Time"] = time;
            return frame;
        };
        stage.push_back(key(0.0, i == 0));
        if (i > 0) stage.push_back(key(step * static_cast<double>(i), true));
        if (i + 1 < frames.size()) stage.push_back(key(step * static_cast<double>(i + 1), false));
        timelines.push_back(Json{{"spriteuid", i + 1}, {"stage", std::move(stage)}});
    }
    return Json{
        {"actors", std::move(actors)},
        {"customvariables", Json::array({Json{{"VariableName", "FirePosition"}, {"ValueType", 4},
            {"Value", {0.0, 0.0}}}})},
        {"events", nullptr},
        {"labels", nullptr},
        {"stageOptions", Json{{"SpriteInfo", std::move(sprite_info)}, {"StageLength", length}, {"Version", 1}}},
        {"timelines", std::move(timelines)},
    };
}

Json build_sprite_upgrade_definition(const CustomTower& tower) {
    const auto& spec = tower.spec;
    const auto cells = custom_sprite_cells(spec);
    Json sprites = Json::object();
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const auto file = custom_sprite_file(tower, i);
        sprites[std::to_string(i)] = file.substr(0, file.size() - 5);
    }
    const auto index_of = [&cells](const std::string& cell) {
        return static_cast<std::size_t>(std::find(cells.begin(), cells.end(), cell) - cells.begin());
    };
    // Each upgrade state "<left><right>" shows the sprite of its highest tier
    // that sets one (path 1 first on a tie), or the base sprite.
    Json levels = Json::object();
    for (std::size_t left = 0; left <= kUpgradeTiers; ++left) {
        for (std::size_t right = 0; right <= kUpgradeTiers; ++right) {
            std::size_t chosen = 0;
            std::size_t chosen_tier = 0;
            for (std::size_t path = 0; path < 2; ++path) {
                const std::size_t bought = path == 0 ? left : right;
                for (std::size_t tier = 1; tier <= bought && tier <= spec.upgrades[path].size(); ++tier) {
                    const auto& sprite = spec.upgrades[path][tier - 1].sprite;
                    if (!sprite.empty() && tier > chosen_tier) {
                        chosen = index_of(sprite);
                        chosen_tier = tier;
                    }
                }
            }
            levels[std::to_string(left) + std::to_string(right)] = chosen;
        }
    }
    return Json{{"Sprites", std::move(sprites)}, {"UpgradeLevels", std::move(levels)}};
}

std::string validate_tower_content(const CustomTowerSpec& spec) {
    std::set<std::string> sheets;
    std::set<std::string> cells;
    for (const auto& texture : spec.textures) {
        if (!sheets.insert(texture.name).second) return "lists texture '" + texture.name + "' twice";
        cells.insert(texture.cells.begin(), texture.cells.end());
    }
    for (const auto& art : {spec.icon, spec.avatar, spec.sprite}) {
        if (!art.empty() && !valid_art_name(art)) return "art name '" + art + "' is invalid";
    }
    // Sprites need their sheet; icons may also name vanilla cells.
    const auto sprite_known = [&cells](const std::string& sprite) { return sprite.empty() || cells.contains(sprite); };
    if (!sprite_known(spec.sprite)) return "sprite '" + spec.sprite + "' is not a cell of the tower's textures";
    for (const auto& frame : spec.sprite_frames) {
        if (!sprite_known(frame)) return "sprite frame '" + frame + "' is not a cell of the tower's textures";
    }
    for (const auto& path : spec.upgrades) {
        for (const auto& tier : path) {
            if (!sprite_known(tier.sprite)) return "upgrade '" + tier.name + "' sprite '" + tier.sprite + "' is not a cell of the tower's textures";
            for (const auto& frame : tier.sprite_frames) {
                if (!sprite_known(frame)) return "upgrade '" + tier.name + "' sprite frame '" + frame + "' is not a cell of the tower's textures";
            }
            if (!tier.sprite.empty() && spec.sprite.empty()) return "upgrade sprites need a tower sprite";
        }
    }
    if (spec.weapons.size() > kMaximumCustomWeapons) {
        return "has more than " + std::to_string(kMaximumCustomWeapons) + " weapons";
    }
    for (std::size_t i = 0; i < spec.weapons.size(); ++i) {
        if (auto error = check_weapon(spec.weapons[i], "weapon " + std::to_string(i + 1)); !error.empty()) return error;
    }
    std::set<std::string> names;
    for (std::size_t path = 0; path < spec.upgrades.size(); ++path) {
        const auto& tiers = spec.upgrades[path];
        const std::string label = path == 0 ? "upgrade path 1" : "upgrade path 2";
        if (!tiers.empty() && tiers.size() != kUpgradeTiers) return label + " needs exactly 4 tiers";
        for (const auto& tier : tiers) {
            if (tier.name.empty() || !valid_text(tier.name, kMaximumNameLength)) {
                return label + " has a missing, too long or invalid upgrade name";
            }
            if (!names.insert(tier.name).second) return "upgrade name '" + tier.name + "' is used twice";
            if (!valid_text(tier.description, TowerRegistry::kMaximumTextLength)) {
                return "upgrade '" + tier.name + "' description is too long or has control characters";
            }
            if (tier.cost < 0 || tier.cost > 1'000'000) return "upgrade '" + tier.name + "' cost is out of range";
            if (tier.weapon) {
                if (auto error = check_weapon(*tier.weapon, "upgrade '" + tier.name + "' weapon"); !error.empty()) {
                    return error;
                }
            }
            if (depth(tier.effect) > kMaximumDepth) return "upgrade '" + tier.name + "' is nested too deeply";
            const auto& weapon_upgrade = tier.effect.value("WeaponUpgrade", Json::object());
            if (weapon_upgrade.contains("SlotIndices")) {
                for (const auto& slot : weapon_upgrade["SlotIndices"]) {
                    const std::size_t slots = spec.weapons.empty() ? 2 : spec.weapons.size();
                    if (!slot.is_number_integer() || slot.get<std::int64_t>() < 0 ||
                        static_cast<std::size_t>(slot.get<std::int64_t>()) >= slots) {
                        return "upgrade '" + tier.name + "' names a weapon slot the tower does not have";
                    }
                }
            }
        }
    }
    // The base tower's upgrades act on its own weapon slots.
    if (!spec.weapons.empty() && (spec.upgrades[0].empty() || spec.upgrades[1].empty())) {
        return "replaces the base tower's weapons, so it must define both upgrade paths";
    }
    return {};
}

void customize_tower_definition(Json& definition, const CustomTower& tower) {
    const auto& spec = tower.spec;
    definition["TypeName"] = tower.native_name;
    if (spec.cost) definition["BaseCost"] = *spec.cost;
    // The shop compares the player's rank with this; the profile's unlock
    // list cannot hold custom towers, so the shop hook skips it for them.
    definition["RankToUnlock"] = spec.unlock_rank;
    if (!spec.xp_progression && definition.contains("UpgradeGateway")) {
        for (auto& path : definition["UpgradeGateway"]) {
            for (auto& gate : path) {
                if (gate.is_object()) {
                    gate["Rank"] = 0;
                    gate["XP"] = 0;
                }
            }
        }
    }
    if (!spec.avatar.empty()) definition["Icon"] = spec.avatar;
    if (!spec.sprite.empty()) definition["SpriteUpgradeDefinition"] = tower.native_name + ".json";
    for (std::size_t path = 0; path < spec.upgrades.size(); ++path) {
        for (std::size_t tier = 0; tier < spec.upgrades[path].size(); ++tier) {
            const auto& upgrade = spec.upgrades[path][tier];
            if (!upgrade.icon.empty()) upgrade_path_entry(definition, "UpgradeIcons", path)[tier] = upgrade.icon;
            if (!upgrade.avatar.empty()) upgrade_path_entry(definition, "UpgradeAvatars", path)[tier] = upgrade.avatar;
        }
    }
    const auto& stats = spec.stats;
    if (stats.camo) definition["CanTargetCamo"] = *stats.camo;
    if (stats.footprint) definition["PlacementRadius"] = *stats.footprint;
    if (!stats.targeting.empty()) definition["TargetingMode"] = stats.targeting;
    if (!stats.placement.empty()) {
        definition["CanBePlacedOnLand"] = stats.placement != "water";
        definition["CanBePlacedInWater"] = stats.placement != "land";
    }
    if (!spec.weapons.empty()) {
        Json weapons = Json::array();
        Json active = Json::array();
        for (std::size_t i = 0; i < spec.weapons.size(); ++i) {
            weapons.push_back(custom_weapon_file(i + 1));
            active.push_back(true);
        }
        definition["DefaultWeapons"] = std::move(weapons);
        definition["ActiveWeaponSlots"] = std::move(active);
    } else if (stats.touches_weapon() && definition.contains("DefaultWeapons") && definition["DefaultWeapons"].is_array() &&
               !definition["DefaultWeapons"].empty() && definition["DefaultWeapons"][0].is_string()) {
        auto& first = definition["DefaultWeapons"][0];
        first = stats_weapon_file(first.get<std::string>());
    }
    for (std::size_t path = 0; path < spec.upgrades.size(); ++path) {
        const auto& tiers = spec.upgrades[path];
        if (tiers.empty()) continue;
        Json names = Json::array();
        Json prices = Json::array();
        for (const auto& tier : tiers) {
            names.push_back(tier.name);
            prices.push_back(tier.cost);
        }
        upgrade_path_entry(definition, "Upgrades", path) = std::move(names);
        upgrade_path_entry(definition, "UpgradePrices", path) = std::move(prices);
    }
}

void apply_weapon_stats(Json& weapon, const CustomTowerStats& stats) {
    if (stats.range) weapon["TargetRange"] = *stats.range;
    if (stats.cooldown) weapon["CooldownTime"] = *stats.cooldown;
    if (!weapon.contains("Tasks")) return;
    Json* const projectile = first_task(weapon["Tasks"], "Projectile");
    if (projectile == nullptr) return;
    if (stats.pierce) (*projectile)["NumPersists"] = *stats.pierce;
    if (stats.projectile_speed && projectile->contains("Movement") && (*projectile)["Movement"].is_object()) {
        (*projectile)["Movement"]["Speed"] = *stats.projectile_speed;
    }
    if (stats.damage && projectile->contains("Tasks") && (*projectile)["Tasks"].is_array()) {
        for (auto& task : (*projectile)["Tasks"]) {
            if (task.is_object() && task.value("Type", std::string()) == "Damage") task["Amount"] = *stats.damage;
        }
    }
}

std::string customize_upgrades(Json& upgrades, const CustomTower& tower) {
    const auto& spec = tower.spec;
    if (spec.upgrades[0].empty() && spec.upgrades[1].empty()) return {};
    if (!upgrades.contains("Upgrades") || !upgrades["Upgrades"].is_array()) return "base upgrades have no list";
    auto& list = upgrades["Upgrades"];
    // Vanilla lists path 1's four tiers, then path 2's.
    while (list.size() < 2 * kUpgradeTiers) list.push_back({{"Name", ""}});
    for (std::size_t path = 0; path < spec.upgrades.size(); ++path) {
        const auto& tiers = spec.upgrades[path];
        for (std::size_t tier = 0; tier < tiers.size(); ++tier) {
            Json entry = Json::object();
            entry["Name"] = tiers[tier].name;
            for (const auto& [key, value] : tiers[tier].effect.items()) entry[key] = value;
            if (tiers[tier].weapon) {
                entry["TowerUpgrade"]["Weapons"]["0"] = custom_upgrade_weapon_file(path, tier + 1);
            }
            list[path * kUpgradeTiers + tier] = std::move(entry);
        }
    }
    return {};
}

}  // namespace btd5loader::runtime
