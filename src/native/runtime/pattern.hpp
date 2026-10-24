// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace btd5loader::runtime {

using Pattern = std::vector<std::optional<std::uint8_t>>;

[[nodiscard]] std::optional<Pattern> parse_pattern(
    std::string_view text,
    std::string& error);
[[nodiscard]] std::vector<std::size_t> find_pattern(
    std::span<const std::byte> bytes,
    const Pattern& pattern);

}  // namespace btd5loader::runtime
