// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "lua_mod.hpp"

namespace btd5loader::runtime {

// Shared by every production Lua dispatch path and by shutdown/startup cleanup.
// A dispatch may re-enter on the thread that already holds the gate: a Lua
// setter such as match:set_cash() raises a native event that is dispatched
// while the outer callback is still running. Other threads still wait.
class ModDispatchGate final {
public:
    class Lease final {
    public:
        Lease(ModDispatchGate& gate, std::unique_lock<std::mutex> lock, const bool allowed) noexcept
            : gate_(&gate), lock_(std::move(lock)), allowed_(allowed) {}
        Lease(Lease&& other) noexcept
            : gate_(std::exchange(other.gate_, nullptr)),
              lock_(std::move(other.lock_)),
              allowed_(other.allowed_) {}
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease& operator=(Lease&&) = delete;
        ~Lease() {
            if (gate_ != nullptr && lock_.owns_lock()) gate_->owner_.store({});
        }
        explicit operator bool() const noexcept { return allowed_; }

    private:
        ModDispatchGate* gate_;
        std::unique_lock<std::mutex> lock_;
        bool allowed_;
    };

    [[nodiscard]] Lease enter() {
        if (owner_.load() == std::this_thread::get_id()) {
            // Nested on the owning thread: the outer lease keeps the lock.
            return {*this, std::unique_lock<std::mutex>{}, !stopping()};
        }
        std::unique_lock lock(mutex_);
        owner_.store(std::this_thread::get_id());
        const bool allowed = !stopping();
        return {*this, std::move(lock), allowed};
    }
    // Startup and shutdown take the gate outright; they never run nested in a dispatch.
    [[nodiscard]] std::unique_lock<std::mutex> exclusive() {
        return std::unique_lock(mutex_);
    }
    [[nodiscard]] bool request_stop() noexcept { return !stopping_.exchange(true); }
    [[nodiscard]] bool stopping() const noexcept { return stopping_.load(); }
private:
    std::mutex mutex_;
    std::atomic<std::thread::id> owner_{};
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
