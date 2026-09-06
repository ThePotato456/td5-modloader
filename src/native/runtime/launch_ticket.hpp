// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "active_profile.hpp"

namespace btd5loader::runtime {
struct LaunchTicket {
    ActiveProfile profile;
    std::filesystem::path data_root;
    std::filesystem::path claimed_path;
    std::filesystem::path acknowledgment_path;
};

// Only a ticket for this exact PID and creation time can authorize mods.
[[nodiscard]] std::optional<LaunchTicket> consume_launch_ticket(
    const std::filesystem::path& game_directory, const std::wstring& token,
    std::int64_t now, std::string& error);
[[nodiscard]] std::wstring current_launch_identity();
}  // namespace btd5loader::runtime
