// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace btd5loader::runtime {

// Encoded custom tower identity. A custom tower's native 64-bit type value is
// the tag bit 63 plus its index spread over the 13 type bits that no vanilla
// tower mask (TargetingFilter, LockedTowers, quest Towers) uses, so vanilla
// masks never match a custom tower and every custom value is distinct; see
// docs/native-internals.md#custom-tower-identities. Index bits are taken
// in this order so small sets use only the bits no vanilla tower owns (60-62),
// then TestTower, the agent planes, the Ninja Kiwi agents, and GameDummy.
inline constexpr std::uint64_t kCustomTowerTag = std::uint64_t{1} << 63;
inline constexpr std::array<std::uint8_t, 13> kCustomTowerIndexBits{
    60, 61, 62, 0, 50, 51, 52, 53, 55, 56, 46, 47, 59};
// Index 0 would be the bare tag, which stays reserved as "any custom tower".
inline constexpr std::uint32_t kMaxCustomTowers = (std::uint32_t{1} << kCustomTowerIndexBits.size()) - 1;

[[nodiscard]] constexpr std::uint64_t custom_tower_index_mask() noexcept {
    std::uint64_t mask = 0;
    for (const auto bit : kCustomTowerIndexBits) mask |= std::uint64_t{1} << bit;
    return mask;
}

// Returns the type value for a custom tower index in [1, kMaxCustomTowers],
// or 0 when the index is out of range.
[[nodiscard]] constexpr std::uint64_t encode_custom_tower(const std::uint32_t index) noexcept {
    if (index == 0 || index > kMaxCustomTowers) return 0;
    std::uint64_t flag = kCustomTowerTag;
    for (std::size_t i = 0; i < kCustomTowerIndexBits.size(); ++i) {
        if ((index >> i) & 1U) flag |= std::uint64_t{1} << kCustomTowerIndexBits[i];
    }
    return flag;
}

// Returns the index a type value encodes, or nothing when the value is not a
// custom tower value: tag missing, any other bit set, or no index bits.
[[nodiscard]] constexpr std::optional<std::uint32_t> decode_custom_tower(const std::uint64_t flag) noexcept {
    if ((flag & kCustomTowerTag) == 0) return std::nullopt;
    if ((flag & ~(kCustomTowerTag | custom_tower_index_mask())) != 0) return std::nullopt;
    std::uint32_t index = 0;
    for (std::size_t i = 0; i < kCustomTowerIndexBits.size(); ++i) {
        if ((flag >> kCustomTowerIndexBits[i]) & 1U) index |= std::uint32_t{1} << i;
    }
    if (index == 0) return std::nullopt;
    return index;
}

}  // namespace btd5loader::runtime
