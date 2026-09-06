// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "lua_mod.hpp"

namespace btd5loader::runtime {

// Shared by every production Lua dispatch path and by shutdown/startup cleanup.
class ModDispatchGate final {
public:
    struct Lease {
        std::unique_lock<std::mutex> lock;
        bool allowed;
        explicit operator bool() const noexcept { return allowed; }
    };
    [[nodiscard]] Lease enter() {
        auto lock = exclusive();
        const bool allowed = !stopping();
        return {std::move(lock), allowed};
    }
    [[nodiscard]] std::unique_lock<std::mutex> exclusive() {
        return std::unique_lock(mutex_);
    }
    [[nodiscard]] bool request_stop() noexcept { return !stopping_.exchange(true); }
    [[nodiscard]] bool stopping() const noexcept { return stopping_.load(); }
private:
    std::mutex mutex_;
    std::atomic<bool> stopping_{};
};

inline void shutdown_mods(std::vector<std::unique_ptr<LuaMod>>& mods) noexcept {
    for (auto iterator = mods.rbegin(); iterator != mods.rend(); ++iterator) {
        try {
            (void)(*iterator)->invoke("on_shutdown");
        } catch (...) {
            // A failed host callback must not prevent another mod's shutdown.
        }
    }
    mods.clear();
}

}  // namespace btd5loader::runtime
