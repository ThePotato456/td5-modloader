// SPDX-License-Identifier: GPL-3.0-only
#include "launch_ticket.hpp"

#include <Windows.h>
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace btd5loader::runtime {
std::wstring current_launch_identity() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return {};
    const auto ticks = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    return std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(ticks);
}

std::optional<LaunchTicket> consume_launch_ticket(
    const std::filesystem::path& game_directory, const std::wstring& token,
    const std::int64_t now, std::string& error) {
    if (token.empty()) return std::nullopt;
    const auto separator = token.find(L'-');
    if (token.size() > 32 || separator == 0 || separator == std::wstring::npos ||
        separator == token.size() - 1 ||
        !std::all_of(token.begin(), token.end(), [](wchar_t c) { return (c >= L'0' && c <= L'9') || c == L'-'; }) ||
        token.find(L'-', separator + 1) != std::wstring::npos) {
        error = "invalid_launch_token";
        return std::nullopt;
    }
    const auto path = game_directory / L".btd5ml-launch" / (token + L".json");
    std::string narrow_token;
    for (const wchar_t c : token) narrow_token.push_back(static_cast<char>(c));
    const auto claimed = std::filesystem::path(path.wstring() + L".claimed");
    // CREATE_NEW is the atomic cross-process claim, including simultaneous readers.
    const HANDLE claim = CreateFileW(claimed.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (claim == INVALID_HANDLE_VALUE) {
        error = "launch_ticket_missing_or_consumed";
        return std::nullopt;
    }
    CloseHandle(claim);
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > 1024U * 1024U) {
        error = "launch_ticket_size_invalid";
        return std::nullopt;
    }
    try {
        std::ifstream input(path, std::ios::binary);
        std::string text(static_cast<std::size_t>(size), '\0');
        if (!input.read(text.data(), static_cast<std::streamsize>(text.size()))) {
            error = "launch_ticket_read_failed";
            return std::nullopt;
        }
        const auto document = nlohmann::json::parse(text);
        if (!document.at("issuedAt").is_number_integer() || !document.at("expiresAt").is_number_integer() ||
            !document.at("processId").is_number_unsigned()) {
            error = "launch_ticket_time_invalid";
            return std::nullopt;
        }
        const auto issued = document.at("issuedAt").get<std::int64_t>();
        const auto expires = document.at("expiresAt").get<std::int64_t>();
        const auto from_utf8 = [](const std::string& value) {
            return std::filesystem::path(std::u8string(value.begin(), value.end()));
        };
        const auto target = from_utf8(document.at("gameDirectory").get<std::string>());
        const auto root = from_utf8(document.at("dataRoot").get<std::string>());
        if (document.at("launchProtocol").get<int>() != 1 ||
            document.at("mode").get<std::string>() != "modded" ||
            std::to_string(document.at("processId").get<std::uint32_t>()) + "-" +
                document.at("processStartedAt").get<std::string>() != narrow_token ||
            issued > now || expires <= now || issued < now - 60 || expires > issued + 60 ||
            expires <= issued || !target.is_absolute() || !root.is_absolute() ||
            !std::filesystem::equivalent(target, game_directory, ec) || ec) {
            error = "launch_ticket_binding_or_expiry_invalid";
            return std::nullopt;
        }
        auto profile = load_active_profile(path, error);
        if (!profile) return std::nullopt;
        return LaunchTicket{std::move(*profile), root, claimed,
            std::filesystem::path(path.wstring() + L".started")};
    } catch (const std::exception&) {
        error = "launch_ticket_malformed";
        return std::nullopt;
    }
}
}  // namespace btd5loader::runtime
