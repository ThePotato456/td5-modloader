// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <string_view>

namespace btd5loader::runtime {

// Type flags of the supported Steam 4.8 build. Bit i of a tower or bloon type
// flag is entry i of the factory's category-0 registration list (tower factory
// RVA 0x1E0B80, bloon factory RVA 0x1AEB20); see
// docs/native-internals.md#native-object-layouts.
inline constexpr std::array<std::string_view, 60> kTowerTypeNames{
    "TestTower", "DartMonkey", "TackTower", "SniperMonkey", "BoomerangThrower",
    "NinjaMonkey", "BombTower", "IceTower", "GlueGunner", "MonkeyBuccaneer",
    "MonkeyAce", "SuperMonkey", "MonkeyApprentice", "MonkeyVillage", "BananaFarm",
    "MortarTower", "DartlingGun", "SpikeFactory", "HeliPilot", "RoadSpikes",
    "ExplodingPineapple", "MonkeyEngineer", "Bloonchipper", "MonkeySub", "MeerkatSpyPro",
    "MeerkatSpy", "TribalTurtlePro", "TribalTurtle", "PortableLakePro", "PortableLake",
    "PontoonPro", "Pontoon", "BloonsdayDevicePro", "BloonsdayDevice", "AngrySquirrelPro",
    "AngrySquirrel", "SuperMonkeyStormPro", "SuperMonkeyStorm", "BeeKeeperPro", "BeeKeeper",
    "BloonberryBushPro", "BloonberryBush", "RadadactylPro", "Radadactyl", "BananaFarmerPro",
    "BananaFarmer", "NinjaKiwiPro", "NinjaKiwi", "WizardLord", "AcePlane",
    "AircraftCarrier", "PhoenixPlane", "SuperPhoenixPlane", "SupplyDropPlane",
    "HeliPilotAircraft", "RadadactylPlane", "RadderdactylPlane", "MonkeyEngineerSentry",
    "MonkeyEngineerSentryTier4", "GameDummy"};

inline constexpr std::array<std::string_view, 20> kBloonTypeNames{
    "TestBloon", "Red", "Blue", "Green", "Yellow", "Pink", "Black", "White", "Lead",
    "Zebra", "Rainbow", "Ceramic", "Golden", "MOAB", "BFB", "ZOMG", "Bloonarius",
    "Blastapopoulos", "Vortex", "Dreadbloon"};

// Returns the name of a single-bit type flag, or an empty view when the flag
// is zero, has several bits, or lies outside the table.
[[nodiscard]] constexpr std::string_view type_flag_name(
    const std::span<const std::string_view> names, const std::uint64_t flag) noexcept {
    if (!std::has_single_bit(flag)) return {};
    const auto index = static_cast<std::size_t>(std::countr_zero(flag));
    return index < names.size() ? names[index] : std::string_view{};
}

}  // namespace btd5loader::runtime
