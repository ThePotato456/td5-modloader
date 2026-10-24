// SPDX-License-Identifier: GPL-3.0-only
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "../../src/native/runtime/custom_tower_art.hpp"
#include "../../src/native/runtime/custom_tower_assets.hpp"
#include "../../src/native/runtime/custom_tower_content.hpp"
#include "../../src/native/runtime/custom_tower_identity.hpp"
#include "../../src/native/runtime/custom_tower_saves.hpp"
#include "../../src/native/runtime/lua_mod.hpp"
#include "../../src/native/runtime/mod_lifecycle.hpp"
#include "../../src/native/runtime/object_types.hpp"
#include "../../src/native/runtime/tower_progress.hpp"
#include "../../src/native/runtime/tower_registry.hpp"

using namespace btd5loader::runtime;
using Catch::Approx;

namespace {

// Every vanilla type bit that appears in a vanilla tower mask: bits 1-45 (the
// shop towers and agents), WizardLord (48), AcePlane (49), HeliPilotAircraft
// (54) and both engineer sentries (57, 58).
constexpr std::uint64_t vanilla_mask_bits() {
    std::uint64_t mask = 0;
    for (int bit = 1; bit <= 45; ++bit) mask |= std::uint64_t{1} << bit;
    for (const int bit : {48, 49, 54, 57, 58}) mask |= std::uint64_t{1} << bit;
    return mask;
}

}  // namespace

TEST_CASE("custom tower values avoid every vanilla mask bit", "[towers][identity]") {
    STATIC_REQUIRE((custom_tower_index_mask() & vanilla_mask_bits()) == 0);
    STATIC_REQUIRE((custom_tower_index_mask() & kCustomTowerTag) == 0);
    STATIC_REQUIRE(kMaxCustomTowers == 8191);
}

TEST_CASE("custom tower values round-trip and stay distinct", "[towers][identity]") {
    std::set<std::uint64_t> seen;
    for (std::uint32_t index = 1; index <= kMaxCustomTowers; ++index) {
        const auto flag = encode_custom_tower(index);
        REQUIRE((flag & kCustomTowerTag) != 0);
        REQUIRE((flag & vanilla_mask_bits()) == 0);
        // Never equal to a single vanilla bit, so exact-match checks never fire.
        REQUIRE(std::popcount(flag) >= 2);
        REQUIRE(decode_custom_tower(flag) == index);
        REQUIRE(seen.insert(flag).second);
    }
    REQUIRE(encode_custom_tower(0) == 0);
    REQUIRE(encode_custom_tower(kMaxCustomTowers + 1) == 0);
}

TEST_CASE("small custom tower sets use only unassigned bits", "[towers][identity]") {
    const std::uint64_t unassigned = (std::uint64_t{7} << 60) | kCustomTowerTag;
    for (std::uint32_t index = 1; index <= 7; ++index) {
        REQUIRE((encode_custom_tower(index) & ~unassigned) == 0);
    }
    REQUIRE(encode_custom_tower(1) == 0x9000000000000000ULL);
}

TEST_CASE("decoding rejects values that are not custom towers", "[towers][identity]") {
    REQUIRE_FALSE(decode_custom_tower(0));
    REQUIRE_FALSE(decode_custom_tower(0x2));  // DartMonkey
    REQUIRE_FALSE(decode_custom_tower(kCustomTowerTag));
    REQUIRE_FALSE(decode_custom_tower(std::uint64_t{1} << 60));
    REQUIRE_FALSE(decode_custom_tower(encode_custom_tower(1) | 0x2));
}

TEST_CASE("the tower registry assigns stable identities", "[towers][registry]") {
    TowerRegistry registry;
    REQUIRE(registry.add("com.example.towers:laser-monkey").empty());
    REQUIRE(registry.add("com.example.towers:frost_cannon").empty());
    REQUIRE(registry.size() == 2);

    const CustomTower* laser = registry.find_id("com.example.towers:laser-monkey");
    REQUIRE(laser != nullptr);
    REQUIRE(laser->index == 1);
    REQUIRE(laser->flag == encode_custom_tower(1));
    REQUIRE(laser->native_name == TowerRegistry::native_name_for(laser->id));
    REQUIRE(laser->native_name.size() == 18);
    REQUIRE(laser->native_name.starts_with("CT"));
    REQUIRE(registry.find_native_name(laser->native_name) == laser);
    REQUIRE(registry.find_flag(laser->flag) == laser);
    REQUIRE(registry.find_flag(encode_custom_tower(3)) == nullptr);

    // Native names depend only on the ID, not on registration order.
    TowerRegistry reversed;
    REQUIRE(reversed.add("com.example.towers:frost_cannon").empty());
    REQUIRE(reversed.add("com.example.towers:laser-monkey").empty());
    REQUIRE(reversed.find_id("com.example.towers:laser-monkey")->native_name == laser->native_name);
}

TEST_CASE("the tower registry rejects bad and duplicate IDs", "[towers][registry]") {
    TowerRegistry registry;
    REQUIRE(registry.add("com.example.towers:laser").empty());
    REQUIRE_FALSE(registry.add("com.example.towers:laser").empty());
    for (const char* id : {"", "laser", "com.example.towers:", "com.example.towers:Laser",
             "Com.example:laser", "nodot:laser", "com.example.towers:1laser",
             "com.example.towers:laser monkey", "com.example.towers:a:b"}) {
        INFO(id);
        REQUIRE_FALSE(registry.add(id).empty());
    }
    REQUIRE(registry.size() == 1);
}

TEST_CASE("custom towers record their base tower and cost", "[towers][registry]") {
    TowerRegistry registry;
    REQUIRE(registry.add("com.example.towers:laser").empty());
    REQUIRE(registry.add("com.example.towers:frost", {.base = "IceTower", .cost = 450, .name = "Frost Cannon"}).empty());
    REQUIRE(registry.find_id("com.example.towers:laser")->spec.base == "DartMonkey");
    REQUIRE_FALSE(registry.find_id("com.example.towers:laser")->spec.cost);
    REQUIRE(registry.find_id("com.example.towers:frost")->spec.base == "IceTower");
    REQUIRE(registry.find_id("com.example.towers:frost")->spec.cost == 450);
    REQUIRE(registry.find_id("com.example.towers:frost")->spec.name == "Frost Cannon");
    REQUIRE(registry.find_id("com.example.towers:laser")->base_flag == 0x2);
    REQUIRE(registry.find_id("com.example.towers:frost")->base_flag == 0x80);
    REQUIRE_FALSE(registry.add("com.example.towers:bad", {.base = "NotATower"}).empty());
    REQUIRE_FALSE(registry.add("com.example.towers:cheap", {.cost = -1}).empty());
    REQUIRE_FALSE(registry.add("com.example.towers:long", {.name = std::string(201, 'x')}).empty());
    REQUIRE_FALSE(registry.add("com.example.towers:ctrl", {.description = "a\nb"}).empty());
    REQUIRE(registry.towers().size() == 2);
}

TEST_CASE("tower type names cover vanilla and custom towers", "[towers][registry]") {
    TowerRegistry registry;
    REQUIRE(registry.add("com.example.towers:laser").empty());
    REQUIRE(registry.type_name(0x2) == "DartMonkey");
    REQUIRE(registry.type_name(std::uint64_t{1} << 59) == "GameDummy");
    REQUIRE(registry.type_name(encode_custom_tower(1)) == "com.example.towers:laser");
    REQUIRE(registry.type_name(encode_custom_tower(2)).empty());
    REQUIRE(registry.type_name(0x6).empty());
}

TEST_CASE("custom asset requests map to their vanilla sources", "[towers][assets]") {
    TowerRegistry empty;
    REQUIRE_FALSE(classify_custom_asset("Assets/JSON/TowerDefinitions/CacheList.json", empty));

    TowerRegistry registry;
    REQUIRE(registry.add("com.example.towers:frost", {.base = "IceTower", .cost = 450}).empty());
    const CustomTower& frost = *registry.find_id("com.example.towers:frost");
    const std::string json = "Assets/JSON/";

    const auto cache = classify_custom_asset(json + "TowerDefinitions/CacheList.json", registry);
    REQUIRE(cache);
    REQUIRE(cache->kind == CustomAssetKind::CacheList);
    REQUIRE(classify_custom_asset(json + "ScreenDefinitions/TowerSelectionScreen/TowerOrderTop.json", registry)->kind ==
            CustomAssetKind::TowerOrder);

    const auto definition = classify_custom_asset(json + "TowerDefinitions/" + frost.native_name + ".tower", registry);
    REQUIRE(definition);
    REQUIRE(definition->kind == CustomAssetKind::TowerFile);
    REQUIRE(definition->tower == &frost);
    REQUIRE(definition->source_path == json + "TowerDefinitions/IceTower.tower");
    REQUIRE(classify_custom_asset(json + "WeaponDefinitions/" + frost.native_name + "/IceTower.weapon", registry)
                ->source_path == json + "WeaponDefinitions/IceTower/IceTower.weapon");
    REQUIRE(classify_custom_asset(json + "UpgradeDefinitions/" + frost.native_name + ".upgrades", registry)
                ->source_path == json + "UpgradeDefinitions/IceTower.upgrades");

    REQUIRE_FALSE(classify_custom_asset(json + "TowerDefinitions/IceTower.tower", registry));
    REQUIRE_FALSE(classify_custom_asset(json + "TowerDefinitions/CT0000000000000000.tower", registry));
    REQUIRE_FALSE(classify_custom_asset("Assets/Textures/" + frost.native_name + ".png", registry));
}

TEST_CASE("custom asset contents are built from vanilla text", "[towers][assets]") {
    TowerRegistry registry;
    REQUIRE(registry.add("com.example.towers:frost",
                {.base = "IceTower", .cost = 450, .shop_first = true})
                .empty());
    REQUIRE(registry.add("com.example.towers:laser", {.xp_progression = true}).empty());
    const CustomTower& frost = *registry.find_id("com.example.towers:frost");
    const CustomTower& laser = *registry.find_id("com.example.towers:laser");
    std::string error;

    const std::string definition =
        "{\n\t\"BaseCost\" : 300,\n\t\"RankToUnlock\" : 12,\n\t\"TypeName\" : \"IceTower\",\n"
        "\t\"UpgradeGateway\" : [ [ { \"Rank\" : 3, \"XP\" : 40 } ] ]\n}";
    const auto tower = build_custom_asset(*classify_custom_asset(
        "Assets/JSON/TowerDefinitions/" + frost.native_name + ".tower", registry), definition, registry, error);
    REQUIRE(tower);
    const auto frost_definition = nlohmann::json::parse(*tower);
    REQUIRE(frost_definition["TypeName"] == frost.native_name);
    REQUIRE(frost_definition["BaseCost"] == 450);
    REQUIRE(frost_definition["RankToUnlock"] == 0);
    REQUIRE(frost_definition["UpgradeGateway"][0][0] == nlohmann::json{{"Rank", 0}, {"XP", 0}});

    const auto cache = build_custom_asset(*classify_custom_asset(
        "Assets/JSON/TowerDefinitions/CacheList.json", registry), R"({"Files":["IceTower.tower"]})", registry, error);
    REQUIRE(cache);
    const auto files = nlohmann::json::parse(*cache).at("Files");
    REQUIRE(files.size() == 3);
    REQUIRE(files[1] == frost.native_name + ".tower");
    REQUIRE(files[2] == laser.native_name + ".tower");

    const auto order = build_custom_asset(*classify_custom_asset(
        "Assets/JSON/ScreenDefinitions/TowerSelectionScreen/TowerOrderTop.json", registry),
        "{ \"Items\" : [ { \"Icon\" : \"dart_monkey_icon\", \"FactoryName\" : \"DartMonkey\", \"KeyboardShortcut\" : \"q\" },\n"
        "\t{ \"Icon\" : \"ice_icon\", \"FactoryName\" : \"IceTower\", \"KeyboardShortcut\" : \"a\" } ] }",
        registry, error);
    REQUIRE(order);
    const auto items = nlohmann::json::parse(*order).at("Items");
    // frost asked to lead the shop; laser follows the vanilla towers.
    REQUIRE(items.size() == 4);
    REQUIRE(items[0]["FactoryName"] == frost.native_name);
    REQUIRE(items[0]["Icon"] == "ice_icon");
    REQUIRE(items[0]["KeyboardShortcut"] == "");
    REQUIRE(items[1]["FactoryName"] == "DartMonkey");
    REQUIRE(items[2]["FactoryName"] == "IceTower");
    REQUIRE(items[3]["FactoryName"] == laser.native_name);
    REQUIRE(items[3]["Icon"] == "dart_monkey_icon");

    // With XP progression the base tower's XP gates stay.
    const auto gated = build_custom_asset(*classify_custom_asset(
        "Assets/JSON/TowerDefinitions/" + laser.native_name + ".tower", registry), definition, registry, error);
    REQUIRE(gated);
    const auto gated_definition = nlohmann::json::parse(*gated);
    REQUIRE(gated_definition["UpgradeGateway"][0][0] == nlohmann::json{{"Rank", 3}, {"XP", 40}});
    REQUIRE(gated_definition["BaseCost"] == 300);

    REQUIRE_FALSE(build_custom_asset(*classify_custom_asset(
        "Assets/JSON/TowerDefinitions/CacheList.json", registry), "not json", registry, error));
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("Lua mods register custom towers while their script loads", "[towers][lua]") {
    TowerRegistry registry;
    LuaModOptions options;
    options.mod_id = "com.example.towers";
    options.register_tower = [&registry](std::string_view id, CustomTowerSpec spec) {
        return registry.add(id, std::move(spec));
    };
    LuaMod mod(options);
    REQUIRE(mod.load_script(R"(
        assert(btd5.towers.register{ id = "laser" })
        assert(btd5.towers.register{ id = "frost", base = "IceTower", cost = 450, name = "Frost Cannon",
            description = "Freezes bloons.", shop = "first", progression = "xp", restricted_modes = "always" })
        assert(btd5.towers.register{ id = "solo", restricted_modes = "never" })
        local ok, message = pcall(btd5.towers.register, { id = "when", restricted_modes = "sometimes" })
        assert(not ok and message:find("restricted_modes"))
        local ok, message = pcall(btd5.towers.register, { id = "how", progression = "slow" })
        assert(not ok and message:find("progression"))
        local ok, message = pcall(btd5.towers.register, { id = "where", shop = "middle" })
        assert(not ok and message:find("shop"))
        local ok, message = pcall(btd5.towers.register, { id = "frost" })
        assert(not ok and message:find("duplicate"))
        ok, message = pcall(btd5.towers.register, { id = "bad", base = "Nope" })
        assert(not ok and message:find("unknown base"))
        ok, message = pcall(btd5.towers.register, { id = "odd", cost = 1.5 })
        assert(not ok and message:find("integer"))
        function on_ready()
            local ok, message = pcall(btd5.towers.register, { id = "late" })
            assert(not ok and message:find("while the mod script loads"))
        end
    )", "main.lua"));
    REQUIRE(mod.invoke("on_ready"));
    REQUIRE(registry.size() == 3);
    REQUIRE(registry.find_id("com.example.towers:laser")->spec.base == "DartMonkey");
    REQUIRE(registry.find_id("com.example.towers:frost")->spec.cost == 450);
    REQUIRE(registry.find_id("com.example.towers:frost")->spec.name == "Frost Cannon");
    REQUIRE(registry.find_id("com.example.towers:frost")->spec.description == "Freezes bloons.");
    REQUIRE(registry.find_id("com.example.towers:frost")->spec.shop_first);
    REQUIRE(registry.find_id("com.example.towers:frost")->spec.xp_progression);
    REQUIRE_FALSE(registry.find_id("com.example.towers:laser")->spec.xp_progression);
    REQUIRE_FALSE(registry.find_id("com.example.towers:laser")->spec.shop_first);
    REQUIRE(registry.find_id("com.example.towers:laser")->spec.restricted_modes == RestrictedModes::base);
    REQUIRE(registry.find_id("com.example.towers:frost")->spec.restricted_modes == RestrictedModes::always);
    REQUIRE(registry.find_id("com.example.towers:solo")->spec.restricted_modes == RestrictedModes::never);
    REQUIRE(registry.find_id("com.example.towers:late") == nullptr);
}

TEST_CASE("custom tower text resolves to mod text or the base tower's", "[towers][assets]") {
    TowerRegistry registry;
    REQUIRE(registry.add("com.example.towers:frost",
                {.base = "IceTower", .name = "Frost Cannon", .description = "Freezes everything."})
                .empty());
    REQUIRE(registry.add("com.example.towers:plain", {.base = "IceTower"}).empty());
    const std::string f = registry.find_id("com.example.towers:frost")->native_name;
    const std::string p = registry.find_id("com.example.towers:plain")->native_name;
    REQUIRE_FALSE(is_shared_custom_asset("Assets/Loc/English.xml"));

    const auto name = resolve_custom_text("LOC_" + f + "_TOWER", registry);
    REQUIRE(name);
    REQUIRE(name->text == "Frost Cannon");
    REQUIRE(resolve_custom_text("LOC_" + f + "_TOWER_PLURAL", registry)->text == "Frost Cannons");
    REQUIRE(resolve_custom_text("LOC_" + f + "_TOWER_CAPS", registry)->text == "FROST CANNON");
    REQUIRE(resolve_custom_text("LOC_TOWER_DESC_" + f, registry)->text == "Freezes everything.");
    REQUIRE(resolve_custom_text("LOC_TOWER_DESC_OSX_" + f, registry)->text == "Freezes everything.");
    REQUIRE(resolve_custom_text("LOC_TOWER_DESC_OSX_" + p, registry)->base_key == "LOC_TOWER_DESC_OSX_IceTower");

    // Everything else, and towers without their own text, use the base tower's ID.
    const auto upgrade = resolve_custom_text("LOC_UPGRADE_A1_" + f, registry);
    REQUIRE(upgrade);
    REQUIRE_FALSE(upgrade->text);
    REQUIRE(upgrade->base_key == "LOC_UPGRADE_A1_IceTower");
    REQUIRE(resolve_custom_text("LOC_" + f + "_UPGRADE_DESC_A1", registry)->base_key == "LOC_IceTower_UPGRADE_DESC_A1");
    const auto plain = resolve_custom_text("LOC_" + p + "_TOWER", registry);
    REQUIRE_FALSE(plain->text);
    REQUIRE(plain->base_key == "LOC_IceTower_TOWER");

    REQUIRE_FALSE(resolve_custom_text("LOC_IceTower_TOWER", registry));
    REQUIRE_FALSE(resolve_custom_text("LOC_" + f + "X_TOWER", registry));
    REQUIRE_FALSE(resolve_custom_text("LOC_CT0000000000000000_TOWER", registry));
}

TEST_CASE("custom tower progress persists XP by tower ID", "[towers][progress]") {
    const auto directory = std::filesystem::temp_directory_path() / "btd5ml-tower-progress-test";
    std::filesystem::remove_all(directory);
    const auto path = directory / "progress.json";
    std::string error;
    {
        CustomTowerProgress progress;
        REQUIRE(progress.load(path, error));
        REQUIRE(progress.xp("com.example.towers:laser") == 0.0F);
        progress.set_xp("com.example.towers:laser", 125.5F);
        progress.set_xp("com.example.towers:bad", -1.0F);
        REQUIRE(progress.xp("com.example.towers:laser") == 125.5F);
        REQUIRE(progress.save(error));
    }
    {
        CustomTowerProgress progress;
        REQUIRE(progress.load(path, error));
        REQUIRE(progress.xp("com.example.towers:laser") == 125.5F);
        REQUIRE(progress.xp("com.example.towers:other") == 0.0F);
        REQUIRE(progress.xp("com.example.towers:bad") == 0.0F);
    }
    {
        std::ofstream(path) << "{ not json";
        CustomTowerProgress progress;
        REQUIRE_FALSE(progress.load(path, error));
        REQUIRE(progress.xp("com.example.towers:laser") == 0.0F);
    }
    std::filesystem::remove_all(directory);
}

namespace {

std::string encode_dgdata(const std::string& json) {
    std::string bytes = "DGDATA00000000";
    for (std::size_t i = 0; i < json.size(); ++i) {
        bytes.push_back(static_cast<char>(static_cast<unsigned char>(json[i]) + 21 + i % 6));
    }
    return bytes;
}

void write_bytes(const std::filesystem::path& path, const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << bytes;
}

std::string game_save_with(const std::string& native) {
    return R"({"Towers":[{"Type":"DartMonkey"},{"Type":")" + native + R"("}]})";
}

}  // namespace

namespace {

using OrderedJson = nlohmann::ordered_json;

const char* const kDartDefinition = R"({
    "BaseCost" : 200, "CanBePlacedInWater" : false, "CanBePlacedOnLand" : true, "CanTargetCamo" : false,
    "DefaultWeapons" : [ "DartMonkey.weapon", "FanClubDart.weapon" ], "ActiveWeaponSlots" : [ true, false ],
    "PlacementRadius" : 8.0, "RankToUnlock" : 0, "TargetingMode" : "First", "TypeName" : "DartMonkey",
    "Upgrades" : [ [ "Long Range Darts", "Enhanced Eyesight", "Spike-O-Pult", "Juggernaut" ],
                   [ "Sharp Shots", "Razor Sharp Shots", "Triple Darts", "Super Monkey Fan Club" ] ],
    "UpgradeGateway" : [ [ { "Rank" : 2, "XP" : 10 } ] ],
    "UpgradePrices" : [ [ 90, 120, 500, 1500 ], [ 140, 170, 330, 8000 ] ]
})";

const char* const kDartWeapon = R"({
    "Type": "DartMonkey", "TargetRange": 100.0, "CooldownTime": 0.7, "FireDelayTime": 0.2,
    "Tasks": [ { "Type": "Projectile", "NumPersists": 1,
                 "Movement": { "Type": "Forward", "Speed": 805.0, "CutOffDistance": 161 },
                 "Tasks": [ { "Type": "Damage", "DamageType": "Piercing", "Amount": 1 } ] } ]
})";

OrderedJson simple_weapon(const double range = 120) {
    return OrderedJson::parse(R"({"Type":"DartMonkey","TargetRange":)" + std::to_string(range) +
                              R"(,"CooldownTime":0.5,"FireDelayTime":0.0,"Tasks":[{"Type":"Projectile","NumPersists":2,)"
                              R"("Movement":{"Type":"Forward","Speed":600},"Tasks":[{"Type":"Damage","DamageType":"Fire","Amount":2}]}]})");
}

std::vector<CustomUpgrade> four_tiers(const std::string& prefix) {
    std::vector<CustomUpgrade> tiers;
    for (int i = 1; i <= 4; ++i) {
        CustomUpgrade tier;
        REQUIRE(parse_upgrade_tier(OrderedJson{{"name", prefix + std::to_string(i)}, {"cost", 100 * i}, {"range", 10}},
                    tier)
                    .empty());
        tiers.push_back(std::move(tier));
    }
    return tiers;
}

}  // namespace

TEST_CASE("custom tower stats parse and reject bad values", "[towers][content]") {
    CustomTowerStats stats;
    REQUIRE(parse_tower_stats(OrderedJson{{"range", 140}, {"cooldown", 0.4}, {"pierce", 3}, {"damage", 2},
                                  {"projectile_speed", 900}, {"footprint", 10}, {"camo", true},
                                  {"targeting", "Strong"}, {"placement", "both"}},
                stats)
                .empty());
    REQUIRE(stats.range == 140);
    REQUIRE(stats.pierce == 3);
    REQUIRE(stats.targeting == "Strong");
    REQUIRE(stats.touches_weapon());
    CustomTowerStats bad;
    REQUIRE(parse_tower_stats(OrderedJson{{"range", -5}}, bad).find("range") != std::string::npos);
    REQUIRE(parse_tower_stats(OrderedJson{{"pierce", 1.5}}, bad).find("pierce") != std::string::npos);
    REQUIRE(parse_tower_stats(OrderedJson{{"targeting", "Weakest"}}, bad).find("targeting") != std::string::npos);
    REQUIRE(parse_tower_stats(OrderedJson{{"placement", "air"}}, bad).find("placement") != std::string::npos);
    REQUIRE(parse_tower_stats(OrderedJson{{"speed", 1}}, bad).find("unknown field 'speed'") != std::string::npos);
}

TEST_CASE("custom weapons and upgrade tiers are checked against the game's types", "[towers][content]") {
    OrderedJson weapon;
    REQUIRE(parse_tower_weapon(OrderedJson::parse(R"({"TargetRange":100,"CooldownTime":1,"Tasks":[{"Type":"Projectile",
        "Tasks":[{"Type":"AreaOfEffect","Range":30,"Tasks":[{"Type":"Damage","DamageType":"Explosive","Amount":1}]}]}]})"),
                weapon)
                .empty());
    REQUIRE(weapon["Type"] == "DartMonkey");  // the default weapon type
    REQUIRE(parse_tower_weapon(OrderedJson::parse(R"({"TargetRange":100,"CooldownTime":1,"Tasks":[{"Type":"Laser"}]})"),
                weapon)
                .find("unknown Type 'Laser'") != std::string::npos);
    REQUIRE(parse_tower_weapon(OrderedJson::parse(
                R"({"TargetRange":100,"CooldownTime":1,"Tasks":[{"Type":"Damage","DamageType":"Cold","Amount":1}]})"),
                weapon)
                .find("DamageType") != std::string::npos);
    REQUIRE(parse_tower_weapon(OrderedJson::parse(R"({"CooldownTime":1,"Tasks":[{"Type":"Damage"}]})"), weapon)
                .find("TargetRange") != std::string::npos);
    REQUIRE(parse_tower_weapon(OrderedJson::parse(R"({"TargetRange":100,"CooldownTime":1,"Tasks":[]})"), weapon)
                .find("at least one task") != std::string::npos);

    CustomUpgrade tier;
    REQUIRE(parse_upgrade_tier(OrderedJson{{"name", "Hot Shots"}, {"cost", 250}, {"description", "Burns."},
                                   {"range", 20}, {"cooldown", -0.1}, {"pierce", 1}, {"camo", true}},
                tier)
                .empty());
    REQUIRE(tier.effect["WeaponUpgrade"]["SlotIndices"] == OrderedJson::array({0}));
    REQUIRE(tier.effect["WeaponUpgrade"]["Adjust"]["TargetRange"] == 20);
    REQUIRE(tier.effect["WeaponUpgrade"]["TaskUpgrade"][1]["NumPersists"] == 1);
    REQUIRE(tier.effect["TowerUpgrade"]["Replace"]["CanTargetCamo"] == true);
    REQUIRE(parse_upgrade_tier(OrderedJson{{"name", "Free"}}, tier).find("needs a cost") != std::string::npos);
    REQUIRE(parse_upgrade_tier(OrderedJson{{"cost", 5}}, tier).find("needs a name") != std::string::npos);
}

TEST_CASE("custom tower content is validated as a whole", "[towers][content]") {
    TowerRegistry registry;
    CustomTowerSpec three_tiers;
    three_tiers.upgrades[0] = four_tiers("A");
    three_tiers.upgrades[0].pop_back();
    REQUIRE(registry.add("com.example.towers:short", three_tiers).find("exactly 4 tiers") != std::string::npos);

    CustomTowerSpec weapons_only;
    weapons_only.weapons.push_back(simple_weapon());
    REQUIRE(registry.add("com.example.towers:half", weapons_only).find("both upgrade paths") != std::string::npos);

    CustomTowerSpec duplicate_names;
    duplicate_names.upgrades[0] = four_tiers("Same");
    duplicate_names.upgrades[1] = four_tiers("Same");
    REQUIRE(registry.add("com.example.towers:twice", duplicate_names).find("used twice") != std::string::npos);

    CustomTowerSpec bad_slot;
    bad_slot.weapons.push_back(simple_weapon());
    bad_slot.upgrades[0] = four_tiers("A");
    bad_slot.upgrades[1] = four_tiers("B");
    bad_slot.upgrades[1][0].effect["WeaponUpgrade"]["SlotIndices"] = OrderedJson::array({1});
    REQUIRE(registry.add("com.example.towers:slot", bad_slot).find("weapon slot") != std::string::npos);

    CustomTowerSpec full;
    full.weapons.push_back(simple_weapon());
    full.upgrades[0] = four_tiers("A");
    full.upgrades[1] = four_tiers("B");
    REQUIRE(registry.add("com.example.towers:full", full).empty());
    REQUIRE(registry.size() == 1);
}

TEST_CASE("custom tower files carry stats, weapons and upgrades", "[towers][content][assets]") {
    TowerRegistry registry;
    CustomTowerSpec tuned;
    tuned.cost = 300;
    REQUIRE(parse_tower_stats(OrderedJson{{"range", 150}, {"cooldown", 0.35}, {"pierce", 4}, {"damage", 3},
                                  {"projectile_speed", 1000}, {"camo", true}, {"footprint", 12},
                                  {"targeting", "Close"}, {"placement", "water"}},
                tuned.stats)
                .empty());
    tuned.upgrades[1] = four_tiers("Hot");
    tuned.upgrades[1][3].weapon = simple_weapon(300);
    tuned.upgrades[1][3].description = "Melts everything.";
    REQUIRE(registry.add("com.example.towers:tuned", tuned).empty());
    CustomTowerSpec armed;
    armed.weapons = {simple_weapon(), simple_weapon(200)};
    armed.upgrades[0] = four_tiers("Arm");
    armed.upgrades[1] = four_tiers("Leg");
    REQUIRE(registry.add("com.example.towers:armed", armed).empty());
    const CustomTower& tuned_tower = *registry.find_id("com.example.towers:tuned");
    const CustomTower& armed_tower = *registry.find_id("com.example.towers:armed");
    const std::string tuned_root = "Assets/JSON/WeaponDefinitions/" + tuned_tower.native_name + "/";
    std::string error;

    const auto request = classify_custom_asset("Assets/JSON/TowerDefinitions/" + tuned_tower.native_name + ".tower", registry);
    const auto definition = nlohmann::json::parse(*build_custom_asset(*request, kDartDefinition, registry, error));
    REQUIRE(definition["BaseCost"] == 300);
    REQUIRE(definition["CanTargetCamo"] == true);
    REQUIRE(definition["PlacementRadius"] == 12.0);
    REQUIRE(definition["TargetingMode"] == "Close");
    REQUIRE(definition["CanBePlacedInWater"] == true);
    REQUIRE(definition["CanBePlacedOnLand"] == false);
    REQUIRE(definition["DefaultWeapons"] == nlohmann::json::array({"btd5ml_p_DartMonkey.weapon", "FanClubDart.weapon"}));
    REQUIRE(definition["Upgrades"][0][0] == "Long Range Darts");  // path 1 is still the base tower's
    REQUIRE(definition["Upgrades"][1] == nlohmann::json::array({"Hot1", "Hot2", "Hot3", "Hot4"}));
    REQUIRE(definition["UpgradePrices"][1] == nlohmann::json::array({100, 200, 300, 400}));

    // The stats weapon starts from the base tower's first weapon.
    const auto stats_request = classify_custom_asset(tuned_root + "btd5ml_p_DartMonkey.weapon", registry);
    REQUIRE(stats_request->part == CustomAssetPart::StatsWeapon);
    REQUIRE(stats_request->source_path == "Assets/JSON/WeaponDefinitions/DartMonkey/DartMonkey.weapon");
    const auto weapon = nlohmann::json::parse(*build_custom_asset(*stats_request, kDartWeapon, registry, error));
    REQUIRE(weapon["TargetRange"] == 150.0);
    REQUIRE(weapon["CooldownTime"] == 0.35);
    REQUIRE(weapon["Tasks"][0]["NumPersists"] == 4);
    REQUIRE(weapon["Tasks"][0]["Movement"]["Speed"] == 1000.0);
    REQUIRE(weapon["Tasks"][0]["Tasks"][0]["Amount"] == 3);

    // Upgrades: path 2 is replaced, its tier 4 swaps in the mod's weapon.
    const std::string base_upgrades = R"({"Upgrades":[{"Name":"Long Range Darts"},{"Name":"Enhanced Eyesight"},)"
                                      R"({"Name":"Spike-O-Pult"},{"Name":"Juggernaut"},{"Name":"Sharp Shots"},)"
                                      R"({"Name":"Razor Sharp Shots"},{"Name":"Triple Darts"},{"Name":"Super Monkey Fan Club"}]})";
    const auto upgrades = nlohmann::json::parse(*build_custom_asset(
        *classify_custom_asset("Assets/JSON/UpgradeDefinitions/" + tuned_tower.native_name + ".upgrades", registry),
        base_upgrades, registry, error));
    REQUIRE(upgrades["Upgrades"][0]["Name"] == "Long Range Darts");
    REQUIRE(upgrades["Upgrades"][4]["Name"] == "Hot1");
    REQUIRE(upgrades["Upgrades"][4]["WeaponUpgrade"]["Adjust"]["TargetRange"] == 10);
    REQUIRE(upgrades["Upgrades"][7]["TowerUpgrade"]["Weapons"]["0"] == "btd5ml_ub4.weapon");
    const auto upgrade_weapon = classify_custom_asset(tuned_root + "btd5ml_ub4.weapon", registry);
    REQUIRE(upgrade_weapon->part == CustomAssetPart::UpgradeWeapon);
    REQUIRE(upgrade_weapon->source_path.empty());
    REQUIRE(nlohmann::json::parse(*build_custom_asset(*upgrade_weapon, "", registry, error))["TargetRange"] == 300.0);
    REQUIRE(classify_custom_asset(tuned_root + "btd5ml_ub3.weapon", registry)->part == CustomAssetPart::Copy);

    // A tower with its own weapons lists only them, all active.
    const auto armed_definition = nlohmann::json::parse(*build_custom_asset(
        *classify_custom_asset("Assets/JSON/TowerDefinitions/" + armed_tower.native_name + ".tower", registry),
        kDartDefinition, registry, error));
    REQUIRE(armed_definition["DefaultWeapons"] == nlohmann::json::array({"btd5ml_w1.weapon", "btd5ml_w2.weapon"}));
    REQUIRE(armed_definition["ActiveWeaponSlots"] == nlohmann::json::array({true, true}));
    const auto second = classify_custom_asset(
        "Assets/JSON/WeaponDefinitions/" + armed_tower.native_name + "/btd5ml_w2.weapon", registry);
    REQUIRE(second->part == CustomAssetPart::ModWeapon);
    REQUIRE(nlohmann::json::parse(*build_custom_asset(*second, "", registry, error))["TargetRange"] == 200.0);
    REQUIRE(classify_custom_asset("Assets/JSON/WeaponDefinitions/" + armed_tower.native_name + "/btd5ml_w3.weapon",
                registry)
                ->part == CustomAssetPart::Copy);

    // Upgrade text comes from the tiers.
    REQUIRE(resolve_custom_text("LOC_UPGRADE_B2_" + tuned_tower.native_name, registry)->text == "Hot2");
    REQUIRE(resolve_custom_text("LOC_" + tuned_tower.native_name + "_UPGRADE_DESC_B4", registry)->text ==
            "Melts everything.");
    const auto base_text = resolve_custom_text("LOC_UPGRADE_A1_" + tuned_tower.native_name, registry);
    REQUIRE_FALSE(base_text->text);
    REQUIRE(base_text->base_key == "LOC_UPGRADE_A1_DartMonkey");
}

TEST_CASE("mod sounds are registered, served and played by ID", "[towers][sounds]") {
    const auto root = std::filesystem::temp_directory_path() / "btd5ml-sound-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "assets" / "sounds");
    std::ofstream(root / "assets" / "sounds" / "zap.wav", std::ios::binary) << "RIFF";
    std::ofstream(root / "assets" / "sounds" / "zip.wav", std::ios::binary) << "RIFF";
    std::ofstream(root / "assets" / "sounds" / "note.txt", std::ios::binary) << "x";

    TowerRegistry registry;
    REQUIRE(registry.empty());
    REQUIRE(registry.add_sound("com.example.towers:zap",
                {root / "assets/sounds/zap.wav", root / "assets/sounds/zip.wav"}, 0.2)
                .empty());
    REQUIRE_FALSE(registry.empty());
    REQUIRE(registry.add_sound("com.example.towers:zap", {root / "assets/sounds/zap.wav"}, 0.1).find("duplicate") !=
            std::string::npos);
    REQUIRE(registry.add_sound("com.example.towers:txt", {root / "assets/sounds/note.txt"}, 0.1).find("WAV") !=
            std::string::npos);
    REQUIRE(registry.add_sound("com.example.towers:none", {}, 0.1).find("1 to 8") != std::string::npos);
    REQUIRE(registry.add_sound("com.example.towers:slow", {root / "assets/sounds/zap.wav"}, 11).find("cooldown") !=
            std::string::npos);
    const CustomSound& zap = *registry.find_sound("com.example.towers:zap");
    REQUIRE(zap.effect == "BTD5ML_SOUND_1");
    REQUIRE(zap.file_names == std::vector<std::string>{"btd5ml_s1_1.wav", "btd5ml_s1_2.wav"});
    REQUIRE(*registry.find_sound_file("btd5ml_s1_2.wav") == root / "assets/sounds/zip.wav");
    REQUIRE(registry.find_sound_file("tower_place.wav") == nullptr);

    // Mod effects are appended last to the in-match effect list.
    std::string error;
    const auto request = classify_custom_asset("Assets/JSON/Audio/game_sfx.json", registry);
    REQUIRE(request->kind == CustomAssetKind::SoundList);
    REQUIRE(is_shared_custom_asset("Assets/JSON/Audio/game_sfx.json"));
    const auto list = nlohmann::json::parse(*build_custom_asset(*request,
        R"({"SFX":[["EXPLOSION",0.1,["explosion_small.wav"]],["TOWER_PLACE",0.5,["tower_place.wav"]]]})", registry,
        error));
    REQUIRE(list["SFX"].size() == 3);
    REQUIRE(list["SFX"][2] == nlohmann::json::array({"BTD5ML_SOUND_1", 0.2, {"btd5ml_s1_1.wav", "btd5ml_s1_2.wav"}}));
    REQUIRE_FALSE(classify_custom_asset("Assets/JSON/Audio/menu_sfx.json", registry));

    // Lua: register while loading, play by short or full ID.
    TowerRegistry lua_registry;
    std::vector<std::string> played;
    LuaModOptions options;
    options.mod_id = "com.example.towers";
    options.resource_directory = root;
    options.register_sound = [&lua_registry](std::string_view id, std::vector<std::filesystem::path> files,
                                 double cooldown) { return lua_registry.add_sound(id, std::move(files), cooldown); };
    options.play_sound = [&played](std::string_view id) {
        played.emplace_back(id);
        return id == "com.example.towers:zap" ? std::string() : std::string("unknown sound");
    };
    LuaMod mod(options);
    REQUIRE(mod.load_script(R"lua(
        assert(btd5.sounds.register{ id = "zap", files = { "assets/sounds/zap.wav" } })
        local ok, message = pcall(btd5.sounds.register, { id = "out", files = { "../secret.wav" } })
        assert(not ok and message:find("assets/"))
        function on_ready()
            assert(btd5.sounds.play("zap") == true)
            local played, reason = btd5.sounds.play("other.mod:boom")
            assert(played == false and reason == "unknown sound")
            local ok, message = pcall(btd5.sounds.register, { id = "late", files = { "assets/sounds/zip.wav" } })
            assert(not ok and message:find("while the mod script loads"))
        end
    )lua", "main.lua"));
    REQUIRE(mod.invoke("on_ready"));
    REQUIRE(played == std::vector<std::string>{"com.example.towers:zap", "other.mod:boom"});
    REQUIRE(lua_registry.find_sound("com.example.towers:zap") != nullptr);
    std::filesystem::remove_all(root);
}

TEST_CASE("custom tower sprites can animate while firing", "[towers][content][art]") {
    std::string sprite;
    std::vector<std::string> frames;
    double length = 0;
    REQUIRE(parse_sprite(OrderedJson("body"), sprite, frames, length).empty());
    REQUIRE((sprite == "body" && frames.empty()));
    REQUIRE(parse_sprite(OrderedJson{{"frames", {"rest", "fire_1", "fire_2"}}, {"length", 0.3}}, sprite, frames, length)
                .empty());
    REQUIRE(sprite == "rest");
    REQUIRE(frames.size() == 3);
    REQUIRE(length == 0.3);
    REQUIRE(parse_sprite(OrderedJson{{"frames", OrderedJson::array()}}, sprite, frames, length).find("1 to 16") !=
            std::string::npos);
    REQUIRE(parse_sprite(OrderedJson{{"frames", {"a"}}, {"speed", 1}}, sprite, frames, length).find("unknown field") !=
            std::string::npos);

    CustomTexture texture;
    texture.name = "anim_sheet";
    texture.cells = {"rest", "fire_1", "fire_2"};
    TowerRegistry registry;
    CustomTowerSpec spec;
    spec.textures = {texture};
    REQUIRE(parse_sprite(OrderedJson{{"frames", {"rest", "fire_1", "fire_2"}}, {"length", 0.3}}, spec.sprite,
                spec.sprite_frames, spec.sprite_length)
                .empty());
    REQUIRE(registry.add("com.example.towers:anim", spec).empty());
    CustomTowerSpec unknown_frame = spec;
    unknown_frame.sprite_frames.push_back("fire_3");
    REQUIRE(registry.add("com.example.towers:anim2", unknown_frame).find("frame 'fire_3'") != std::string::npos);

    const auto sprite_json = build_tower_sprite(*registry.find_id("com.example.towers:anim"), 0);
    REQUIRE(sprite_json["actors"].size() == 3);
    REQUIRE(sprite_json["actors"][0]["Shown"] == true);
    REQUIRE(sprite_json["actors"][2]["Shown"] == false);
    REQUIRE(sprite_json["stageOptions"]["StageLength"] == 0.3);
    REQUIRE(sprite_json["stageOptions"]["SpriteInfo"][1]["Texture"] == "anim_sheet");
    // The rest frame hides at 0.1; the second frame shows from 0.1 to 0.2; the last stays shown.
    REQUIRE(sprite_json["timelines"][0]["stage"].size() == 2);
    REQUIRE(sprite_json["timelines"][0]["stage"][1]["Shown"] == false);
    REQUIRE(sprite_json["timelines"][1]["stage"][1]["Time"].get<double>() == Approx(0.1));
    REQUIRE(sprite_json["timelines"][1]["stage"][2]["Time"].get<double>() == Approx(0.2));
    REQUIRE(sprite_json["timelines"][2]["stage"].size() == 2);
}

TEST_CASE("custom tower art names are checked against the game", "[towers][content][art]") {
    ArtCatalog catalog;
    catalog.add_cells_from_atlas(R"(<FrameInformation name="InGame"><Cell name="dart_monkey_dart" x="0"/>)"
                                 R"(<Cell name="dart_monkey_icon" x="1"/></FrameInformation>)");
    catalog.add_weapon_sprite("Explosion.json");
    REQUIRE(catalog.has_cell("dart_monkey_dart"));
    REQUIRE_FALSE(catalog.has_cell("laser_beam"));
    TowerRegistry registry;
    registry.set_art_check([&catalog](const bool weapon_sprite, const std::string_view name) {
        return weapon_sprite ? catalog.has_weapon_sprite(name) : catalog.has_cell(name);
    });

    const auto armed = [](const std::string& graphic, const std::string& sprite_file) {
        CustomTowerSpec spec;
        auto weapon = simple_weapon();
        weapon["Tasks"][0]["GraphicName"] = graphic;
        weapon["Tasks"][0]["Tasks"].push_back({{"Type", "Effect"}, {"SpriteFile", sprite_file}});
        spec.weapons.push_back(std::move(weapon));
        spec.upgrades[0] = four_tiers("A");
        spec.upgrades[1] = four_tiers("B");
        return spec;
    };
    REQUIRE(registry.add("com.example.towers:good", armed("dart_monkey_dart", "Explosion.json")).empty());
    REQUIRE(registry.add("com.example.towers:beam", armed("laser_beam", "Explosion.json")) ==
            "custom tower 'com.example.towers:beam' weapon 1 names unknown sprite cell 'laser_beam'");
    REQUIRE(registry.add("com.example.towers:boom", armed("dart_monkey_dart", "Kaboom.json")).find(
                "unknown weapon sprite file 'Kaboom.json'") != std::string::npos);
    CustomTowerSpec bad_icon;
    bad_icon.icon = "no_such_icon";
    REQUIRE(registry.add("com.example.towers:icon", bad_icon).find("icon names unknown sprite cell") != std::string::npos);
    CustomTowerSpec tier_icon;
    tier_icon.upgrades[0] = four_tiers("T");
    tier_icon.upgrades[0][1].icon = "missing_tier_icon";
    REQUIRE(registry.add("com.example.towers:tier", tier_icon).find("upgrade 'T2' icon") != std::string::npos);
    CustomTowerSpec vanilla_icon;
    vanilla_icon.icon = "dart_monkey_icon";
    REQUIRE(registry.add("com.example.towers:vanilla", vanilla_icon).empty());
}

TEST_CASE("custom towers unlock at their rank", "[towers][content]") {
    TowerRegistry registry;
    REQUIRE(registry.add("com.example.towers:late", {.unlock_rank = 30}).empty());
    REQUIRE(registry.add("com.example.towers:bad", {.unlock_rank = 101}).find("unlock_rank") != std::string::npos);
    const CustomTower& late = *registry.find_id("com.example.towers:late");
    std::string error;
    const auto definition = nlohmann::json::parse(*build_custom_asset(
        *classify_custom_asset("Assets/JSON/TowerDefinitions/" + late.native_name + ".tower", registry),
        R"({"TypeName":"DartMonkey","RankToUnlock":0})", registry, error));
    REQUIRE(definition["RankToUnlock"] == 30);

    LuaModOptions options;
    options.mod_id = "com.example.towers";
    options.register_tower = [&registry](std::string_view id, CustomTowerSpec spec) {
        return registry.add(id, std::move(spec));
    };
    LuaMod mod(options);
    REQUIRE(mod.load_script(R"lua(
        assert(btd5.towers.register{ id = "ranked", unlock_rank = 12 })
        local ok, message = pcall(btd5.towers.register, { id = "odd", unlock_rank = 1.5 })
        assert(not ok and message:find("unlock_rank"))
    )lua", "main.lua"));
    REQUIRE(registry.find_id("com.example.towers:ranked")->spec.unlock_rank == 12);
}

TEST_CASE("custom tower art serves mod sheets, icons and sprites", "[towers][content][art]") {
    const auto root = std::filesystem::temp_directory_path() / "btd5ml-tower-art-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "Tablet");
    const auto write = [](const std::filesystem::path& path, const std::string& text) {
        std::ofstream(path, std::ios::binary) << text;
    };
    const std::string atlas = R"(<SpriteInformation><FrameInformation name="laser_art" texw="64" texh="32" type="png">)"
                              R"(<Cell name="laser_body" x="0" y="0" w="32" h="32"/>)"
                              R"(<Cell name="laser_hot" x="32" y="0" w="32" h="32"/></FrameInformation></SpriteInformation>)";
    write(root / "laser_art.png", "PNG-ULTRA");
    write(root / "laser_art.xml", atlas);
    write(root / "Tablet" / "laser_art.png", "PNG-TABLET");
    write(root / "Tablet" / "laser_art.xml", atlas);
    write(root / "wrong.png", "x");
    write(root / "wrong.xml", R"(<FrameInformation name="other"><Cell name="a"/></FrameInformation>)");

    CustomTexture texture;
    REQUIRE(load_custom_texture(root, "laser_art", texture).empty());
    REQUIRE(texture.cells == std::vector<std::string>{"laser_body", "laser_hot"});
    REQUIRE_FALSE(texture.tablet_image.empty());
    CustomTexture bad;
    REQUIRE(load_custom_texture(root, "missing", bad).find("missing.png") != std::string::npos);
    REQUIRE(load_custom_texture(root, "wrong", bad).find("FrameInformation name") != std::string::npos);
    REQUIRE(load_custom_texture(root, "../up", bad).find("letters, digits") != std::string::npos);

    TowerRegistry registry;
    CustomTowerSpec spec;
    spec.textures = {texture};
    spec.icon = "laser_body";
    spec.avatar = "dart_monkey_avatar_small";  // icons may name vanilla cells
    spec.sprite = "laser_body";
    spec.upgrades[1] = four_tiers("Heat");
    spec.upgrades[1][2].sprite = "laser_hot";
    spec.upgrades[1][2].icon = "laser_hot";
    REQUIRE(registry.add("com.example.towers:laser", spec).empty());
    CustomTowerSpec unknown_sprite;
    unknown_sprite.sprite = "nowhere";
    REQUIRE(registry.add("com.example.towers:ghost", unknown_sprite).find("not a cell") != std::string::npos);
    CustomTowerSpec clash;
    clash.textures = {texture};
    clash.textures[0].image = root / "other.png";
    REQUIRE(registry.add("com.example.towers:clash", clash).find("already used") != std::string::npos);
    const CustomTower& laser = *registry.find_id("com.example.towers:laser");
    std::string error;

    // The sheet is added to the sprite index and served at both qualities.
    const auto index_request = classify_custom_asset("Assets/Textures/ExtraSpriteInfo.xml", registry);
    REQUIRE(index_request->kind == CustomAssetKind::SpriteIndex);
    REQUIRE(is_shared_custom_asset("Assets/Textures/ExtraSpriteInfo.xml"));
    const auto index = *build_custom_asset(*index_request,
        "<SpriteInformation>\n    <SpriteInfoXml name = \"cursor\" type = \"png\" />\n</SpriteInformation>", registry, error);
    REQUIRE(index.find(R"(<SpriteInfoXml name="laser_art" type="png"/>)") < index.find("</SpriteInformation>"));
    REQUIRE(index.find("cursor") != std::string::npos);
    REQUIRE(*build_custom_asset(*classify_custom_asset("Assets/Textures/Ultra/laser_art.png", registry), "", registry,
                error) == "PNG-ULTRA");
    REQUIRE(*build_custom_asset(*classify_custom_asset("Assets/Textures/Tablet/laser_art.png", registry), "", registry,
                error) == "PNG-TABLET");
    REQUIRE(*build_custom_asset(*classify_custom_asset("Assets/Textures/Ultra/laser_art.xml", registry), "", registry,
                error) == atlas);
    REQUIRE_FALSE(classify_custom_asset("Assets/Textures/Ultra/InGame.xml", registry));

    // The definition points at a generated sprite-upgrade file and the mod's icons.
    const auto definition = nlohmann::json::parse(*build_custom_asset(
        *classify_custom_asset("Assets/JSON/TowerDefinitions/" + laser.native_name + ".tower", registry),
        R"({"TypeName":"DartMonkey","Icon":"dart_monkey_avatar_small","SpriteUpgradeDefinition":"DartMonkey.json",)"
        R"("UpgradeIcons":[["a1","a2","a3","a4"],["b1","b2","b3","b4"]],"Upgrades":[["A","B","C","D"],["E","F","G","H"]]})",
        registry, error));
    REQUIRE(definition["SpriteUpgradeDefinition"] == laser.native_name + ".json");
    REQUIRE(definition["UpgradeIcons"][1][2] == "laser_hot");
    REQUIRE(definition["UpgradeIcons"][1][1] == "b2");

    const auto upgrade_request = classify_custom_asset(
        "Assets/JSON/TowerSpriteUpgradeDefinitions/" + laser.native_name + ".json", registry);
    REQUIRE(upgrade_request->part == CustomAssetPart::SpriteUpgrade);
    const auto sprites = nlohmann::json::parse(*build_custom_asset(*upgrade_request, "", registry, error));
    REQUIRE(sprites["Sprites"]["0"] == "btd5ml_" + laser.native_name + "_0");
    REQUIRE(sprites["Sprites"]["1"] == "btd5ml_" + laser.native_name + "_1");
    REQUIRE(sprites["UpgradeLevels"]["00"] == 0);
    REQUIRE(sprites["UpgradeLevels"]["02"] == 0);
    REQUIRE(sprites["UpgradeLevels"]["03"] == 1);  // path 2 tier 3 sets the hot sprite
    REQUIRE(sprites["UpgradeLevels"]["24"] == 1);

    const auto sprite_request = classify_custom_asset(
        "Assets/JSON/TowerSprites/btd5ml_" + laser.native_name + "_1.json", registry);
    REQUIRE(sprite_request->part == CustomAssetPart::TowerSprite);
    const auto sprite = nlohmann::json::parse(*build_custom_asset(*sprite_request, "", registry, error));
    REQUIRE(sprite["actors"][0]["sprite"] == "laser_hot");
    REQUIRE(sprite["stageOptions"]["SpriteInfo"][0]["Texture"] == "laser_art");
    REQUIRE_FALSE(classify_custom_asset("Assets/JSON/TowerSprites/btd5ml_" + laser.native_name + "_2.json", registry));

    // The shop button uses the mod's icon.
    const auto order = nlohmann::json::parse(*build_custom_asset(
        *classify_custom_asset("Assets/JSON/ScreenDefinitions/TowerSelectionScreen/TowerOrderTop.json", registry),
        R"({"Items":[{"Icon":"dart_monkey_icon","FactoryName":"DartMonkey"}]})", registry, error));
    REQUIRE(order["Items"][1]["Icon"] == "laser_body");
    std::filesystem::remove_all(root);
}

TEST_CASE("Lua tower content and callbacks register through btd5.towers.register", "[towers][content][lua]") {
    TowerRegistry registry;
    GameObjectRegistry objects;
    std::string tower_type;
    std::vector<std::string> logs;
    LuaModOptions options;
    options.mod_id = "com.example.towers";
    options.object_registry = &objects;
    options.register_tower = [&registry](std::string_view id, CustomTowerSpec spec) {
        return registry.add(id, std::move(spec));
    };
    options.game_object_text_get = [&tower_type](GameObjectKind, void*, std::string_view property)
        -> std::optional<std::string> {
        if (property != "type") return std::nullopt;
        return tower_type;
    };
    options.log = [&logs](std::string_view, std::string_view message) { logs.emplace_back(message); };
    LuaMod mod(options);
    REQUIRE(mod.load_script(R"lua(
        local function tier(name, cost) return { name = name, cost = cost, range = 10 } end
        local laser = {
            TargetRange = 160, CooldownTime = 0.3,
            Tasks = { { Type = "Projectile", NumPersists = 3, Movement = { Type = "Forward", Speed = 900 },
                        Tasks = { { Type = "Damage", DamageType = "Plasma", Amount = 2 } } } },
        }
        assert(btd5.towers.register{
            id = "laser", base = "DartMonkey", cost = 500,
            stats = { camo = true, targeting = "Strong" },
            weapons = { laser },
            upgrades = {
                { tier("Focus", 100), tier("Lens", 200), tier("Prism", 300),
                  { name = "Death Ray", cost = 4000, weapon = laser, description = "Big." } },
                { tier("Cool", 100), tier("Colder", 200), tier("Coldest", 300), tier("Zero", 400) },
            },
            on_place = function(tower) btd5.log("info", "placed " .. tower:type()) end,
            on_sell = function(tower, event) btd5.log("info", "selling " .. tower:type()) end,
        })
        assert(btd5.towers.register{ id = "plain", stats = { range = 120 } })
        local ok, message = pcall(btd5.towers.register, { id = "bad", stats = { range = "far" } })
        assert(not ok and message:find("range"))
        ok, message = pcall(btd5.towers.register, { id = "bad", weapons = { { TargetRange = 1, CooldownTime = 1,
            Tasks = { { Type = "Nope" } } } } })
        assert(not ok and message:find("unknown Type 'Nope'"))
        ok, message = pcall(btd5.towers.register, { id = "bad", stats = { range = function() end } })
        assert(not ok and message:find("unsupported function"))
        ok, message = pcall(btd5.towers.register, { id = "bad", on_place = 5 })
        assert(not ok and message:find("on_place must be a function"))
        ok, message = pcall(btd5.towers.register, { id = "bad", upgrades = { { tier("One", 1) } } })
        assert(not ok and message:find("exactly 4 tiers"))
    )lua", "main.lua"));
    REQUIRE(registry.size() == 2);
    const CustomTower& laser = *registry.find_id("com.example.towers:laser");
    REQUIRE(laser.spec.stats.camo == true);
    REQUIRE(laser.spec.weapons.size() == 1);
    REQUIRE(laser.spec.weapons[0]["Tasks"][0]["Tasks"][0]["DamageType"] == "Plasma");
    REQUIRE(laser.spec.upgrades[0][3].weapon);
    REQUIRE(laser.spec.upgrades[1][3].name == "Zero");
    REQUIRE(registry.find_id("com.example.towers:plain")->spec.stats.range == 120);

    // Callbacks fire only for this tower's type.
    int native{};
    const auto handle = objects.add(GameObjectKind::Tower, &native);
    tower_type = "DartMonkey";
    REQUIRE(mod.dispatch_event("tower.placed", {{"tower", handle}}).succeeded);
    tower_type = "com.example.towers:laser";
    REQUIRE(mod.dispatch_event("tower.placed", {{"tower", handle}}).succeeded);
    REQUIRE(mod.dispatch_event("tower.selling", {{"tower", handle}}).succeeded);
    REQUIRE(mod.dispatch_event("tower.upgraded", {{"tower", handle}}).succeeded);
    REQUIRE(logs == std::vector<std::string>{"placed com.example.towers:laser", "selling com.example.towers:laser"});
}

TEST_CASE("match saves decode and list their custom towers", "[towers][saves]") {
    const std::string json = R"({"Towers":[{"Type":"CT0123456789ABCDEF"},{"Type":"DartMonkey"},)"
                             R"({"Type":"CT0123456789ABCDEF"},{"Type":"CTnothex000000000"},{"Type":"CT0000000000000001"}]})";
    const auto decoded = decode_dgdata(encode_dgdata(json));
    REQUIRE(decoded == json);
    REQUIRE_FALSE(decode_dgdata("NOTDATA000000000"));
    REQUIRE_FALSE(decode_dgdata("DGDATA"));
    REQUIRE(custom_tower_names_in_save(json) == std::vector<std::string>{"CT0000000000000001", "CT0123456789ABCDEF"});
}

TEST_CASE("match saves naming removed towers are held and restored", "[towers][saves]") {
    const auto root = std::filesystem::temp_directory_path() / "btd5ml-tower-saves-test";
    std::filesystem::remove_all(root);
    const auto docs = root / "Docs";
    const auto store = root / "store";

    TowerRegistry with;
    REQUIRE(with.add("com.example.towers:laser").empty());
    const std::string native = with.find_id("com.example.towers:laser")->native_name;
    const std::string save = encode_dgdata(game_save_with(native));
    write_bytes(docs / "Game.save", save);
    write_bytes(docs / "Odyssey.save", encode_dgdata(R"({"Towers":[{"Type":"DartMonkey"}]})"));

    // With the tower registered nothing moves, and its name is remembered.
    REQUIRE(guard_match_saves(docs, store, with).empty());
    REQUIRE(std::filesystem::exists(docs / "Game.save"));

    // Without it the save is held and the tower reported by its ID.
    TowerRegistry without;
    auto lines = guard_match_saves(docs, store, without);
    REQUIRE(lines == std::vector<std::string>{"custom_tower_save_held file=Game.save;missing=com.example.towers:laser"});
    REQUIRE_FALSE(std::filesystem::exists(docs / "Game.save"));
    REQUIRE(std::filesystem::exists(docs / "Odyssey.save"));
    lines = guard_match_saves(docs, store, without);
    REQUIRE(lines == std::vector<std::string>{"custom_tower_save_still_held file=Game.save;missing=com.example.towers:laser"});

    // A new match save while one is held keeps the older one aside.
    write_bytes(docs / "Game.save", encode_dgdata(game_save_with("CT0000000000000001")));
    lines = guard_match_saves(docs, store, without);
    REQUIRE(lines == std::vector<std::string>{"custom_tower_save_held file=Game.save;missing=CT0000000000000001"});
    REQUIRE(std::filesystem::exists(store / "held-saves" / "Game.save.1"));

    // Once the towers are back the newest held save returns unchanged.
    std::filesystem::rename(store / "held-saves" / "Game.save.1", store / "held-saves" / "Game.save");
    REQUIRE(guard_match_saves(docs, store, with) == std::vector<std::string>{"custom_tower_save_restored file=Game.save"});
    {
        std::ifstream restored(docs / "Game.save", std::ios::binary);
        REQUIRE(std::string(std::istreambuf_iterator<char>(restored), {}) == save);
    }
    std::filesystem::remove_all(root);
}

TEST_CASE("the custom tower sample registers its towers", "[towers][lua][samples]") {
    const auto path = std::filesystem::path(BTD5ML_SOURCE_DIR) / "samples" / "custom-tower-mod" / "lua" / "main.lua";
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream);
    const std::string script((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

    TowerRegistry registry;
    LuaModOptions options;
    options.resource_directory = std::filesystem::path(BTD5ML_SOURCE_DIR) / "samples" / "custom-tower-mod";
    options.register_sound = [&registry](std::string_view id, std::vector<std::filesystem::path> files,
                                 double cooldown) { return registry.add_sound(id, std::move(files), cooldown); };
    options.mod_id = "example.custom-towers";
    options.register_tower = [&registry](std::string_view id, CustomTowerSpec spec) {
        return registry.add(id, std::move(spec));
    };
    LuaMod mod(options);
    REQUIRE(mod.load_script(script, "lua/main.lua"));
    REQUIRE(mod.has_event_handlers("tower.placed"));
    REQUIRE(mod.has_event_handlers("tower.upgraded"));
    REQUIRE(mod.has_event_handlers("tower.selling"));
    REQUIRE(registry.size() == 3);
    const CustomTower* const plasma = registry.find_id("example.custom-towers:plasma-monkey");
    REQUIRE(plasma != nullptr);
    REQUIRE(plasma->spec.shop_first);
    REQUIRE(plasma->spec.weapons.size() == 1);
    REQUIRE(plasma->spec.upgrades[0].size() == 4);
    REQUIRE(plasma->spec.upgrades[1][3].weapon);
    REQUIRE(plasma->spec.stats.targeting == "Strong");
    REQUIRE(plasma->spec.textures.size() == 1);
    REQUIRE(plasma->spec.sprite == "plasma_monkey_body");
    REQUIRE(custom_sprite_cells(plasma->spec).size() == 3);
    REQUIRE(plasma->spec.sprite_frames.size() == 3);
    REQUIRE(registry.find_sound("example.custom-towers:plasma-zap") != nullptr);

    const CustomTower* dart = registry.find_id("example.custom-towers:sharp-dart");
    REQUIRE(dart != nullptr);
    REQUIRE(dart->spec.base == "DartMonkey");
    REQUIRE(dart->spec.cost == 150);
    REQUIRE(dart->spec.name == "Sharp Dart Monkey");
    REQUIRE(dart->spec.xp_progression);
    REQUIRE_FALSE(dart->spec.shop_first);

    const CustomTower* frost = registry.find_id("example.custom-towers:frost-cannon");
    REQUIRE(frost != nullptr);
    REQUIRE(frost->spec.base == "IceTower");
    REQUIRE(frost->spec.cost == 400);
    REQUIRE_FALSE(frost->spec.xp_progression);
}
