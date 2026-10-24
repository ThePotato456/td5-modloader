// SPDX-License-Identifier: GPL-3.0-only
#include "pattern.hpp"

#include <charconv>
#include <sstream>
#include <string>

namespace btd5loader::runtime {

std::optional<Pattern> parse_pattern(
    const std::string_view text,
    std::string& error) {
    std::istringstream stream{std::string(text)};
    Pattern pattern;
    std::string token;
    while (stream >> token) {
        if (token == "?" || token == "??") {
            pattern.emplace_back(std::nullopt);
            continue;
        }
        if (token.size() != 2) {
            error = "pattern token must be two hexadecimal digits or ??";
            return std::nullopt;
        }
        unsigned int value = 0;
        const auto parsed = std::from_chars(
            token.data(), token.data() + token.size(), value, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() ||
            value > 0xffU) {
            error = "pattern contains an invalid hexadecimal token";
            return std::nullopt;
        }
        pattern.emplace_back(static_cast<std::uint8_t>(value));
    }
    if (pattern.empty()) {
        error = "pattern cannot be empty";
        return std::nullopt;
    }
    return pattern;
}

std::vector<std::size_t> find_pattern(
    const std::span<const std::byte> bytes,
    const Pattern& pattern) {
    std::vector<std::size_t> matches;
    if (pattern.empty() || pattern.size() > bytes.size()) {
        return matches;
    }

    for (std::size_t offset = 0; offset <= bytes.size() - pattern.size(); ++offset) {
        bool matches_at_offset = true;
        for (std::size_t index = 0; index < pattern.size(); ++index) {
            if (pattern[index].has_value() &&
                std::to_integer<std::uint8_t>(bytes[offset + index]) != *pattern[index]) {
                matches_at_offset = false;
                break;
            }
        }
        if (matches_at_offset) {
            matches.push_back(offset);
        }
    }
    return matches;
}

}  // namespace btd5loader::runtime
