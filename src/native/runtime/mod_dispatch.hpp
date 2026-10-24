// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <algorithm>
#include <limits>
#include "lua_mod.hpp"
#include "lives_write_hook.hpp"

namespace btd5loader::runtime {

// Callers hold the production dispatch gate for the entire profile traversal.
template<class Mods>
bool dispatch_profile_event(const Mods& mods, const std::string_view name,
                            const LuaEventFields& fields, const bool cancellable) {
    bool cancelled = false;
    for (const auto& mod : mods) {
        cancelled = mod->dispatch_event(name, fields, cancellable).cancelled || cancelled;
    }
    return cancelled;
}

template<class Mods, class ReportInvalid>
LivesChangeDecision dispatch_profile_lives(const Mods& mods, const std::int32_t before,
                                         const std::int32_t after, ReportInvalid report_invalid,
                                         const GameObjectHandle player = {}) {
    std::int64_t new_lives = after;
    bool cancelled = false;
    for (const auto& mod : mods) {
        LuaEventFields fields{{"old_lives", static_cast<std::int64_t>(before)},
                              {"new_lives", new_lives}};
        if (player.id != 0) fields.emplace_back("player", player);
        const auto result = mod->dispatch_event("lives.changing", fields, true);
        cancelled = result.cancelled || cancelled;
        const auto mutation = std::find_if(result.fields.begin(), result.fields.end(),
            [](const auto& field) { return field.first == "new_lives"; });
        if (mutation != result.fields.end() && std::holds_alternative<std::int64_t>(mutation->second)) {
            const auto proposed = std::get<std::int64_t>(mutation->second);
            if (proposed >= 0 && proposed <= (std::numeric_limits<std::int32_t>::max)()) {
                new_lives = proposed;
            } else {
                report_invalid();
            }
        }
    }
    return {cancelled, static_cast<std::int32_t>(new_lives)};
}

}  // namespace btd5loader::runtime
