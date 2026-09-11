// SPDX-License-Identifier: GPL-3.0-only
#include "round_hook.hpp"

#include <MinHook.h>

#include <utility>

namespace btd5loader::runtime {

RoundHook* RoundHook::active_{};
RoundHook::StartFunction RoundHook::original_{};

RoundHook::~RoundHook() { remove(); }

bool RoundHook::install(void* const target, Callback starting, std::string& error) {
    if (installed_ || active_ != nullptr) {
        error = "round hook is already installed";
        return false;
    }
    if (target == nullptr || !starting) {
        error = "round hook target or callback is missing";
        return false;
    }
    const MH_STATUS initialize_status = MH_Initialize();
    if (initialize_status != MH_OK && initialize_status != MH_ERROR_ALREADY_INITIALIZED) {
        error = "MinHook initialization failed";
        return false;
    }
    StartFunction original = nullptr;
    if (MH_CreateHook(
            target,
            reinterpret_cast<void*>(&hooked_start),
            reinterpret_cast<void**>(&original)) != MH_OK) {
        error = "round-start producer hook creation failed";
        return false;
    }
    target_ = target;
    starting_ = std::move(starting);
    original_ = original;
    active_ = this;
    if (MH_EnableHook(target_) != MH_OK) {
        active_ = nullptr;
        original_ = nullptr;
        starting_ = {};
        (void)MH_RemoveHook(target_);
        target_ = nullptr;
        error = "round-start producer hook enable failed";
        return false;
    }
    installed_ = true;
    return true;
}

void RoundHook::remove() noexcept {
    if (!installed_) return;
    (void)MH_DisableHook(target_);
    active_ = nullptr;
    (void)MH_RemoveHook(target_);
    original_ = nullptr;
    starting_ = {};
    target_ = nullptr;
    installed_ = false;
}

bool RoundHook::installed() const noexcept { return installed_; }

void __fastcall RoundHook::hooked_start(
    void* const round,
    void*,
    void* const number) noexcept {
    RoundHook* const active = active_;
    if (active != nullptr && active->starting_) {
        try {
            active->starting_(round);
        } catch (...) {
            // Keep the native round-start boundary exception-safe.
        }
    }
    const StartFunction original = original_;
    if (original != nullptr) original(round, number);
}

}  // namespace btd5loader::runtime
