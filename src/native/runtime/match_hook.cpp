// SPDX-License-Identifier: GPL-3.0-only
#include "match_hook.hpp"

#include <MinHook.h>

#include <utility>

namespace btd5loader::runtime {

MatchHook* MatchHook::active_{};
MatchHook::InitFunction MatchHook::original_{};

MatchHook::~MatchHook() {
    remove();
}

bool MatchHook::install(
    void* const target,
    Callback starting,
    Callback started,
    std::string& error) {
    if (installed_ || active_ != nullptr) {
        error = "match hook is already installed";
        return false;
    }
    if (target == nullptr || !starting || !started) {
        error = "match hook target or callback is missing";
        return false;
    }
    const MH_STATUS initialize_status = MH_Initialize();
    if (initialize_status != MH_OK && initialize_status != MH_ERROR_ALREADY_INITIALIZED) {
        error = "MinHook initialization failed";
        return false;
    }

    InitFunction original = nullptr;
    if (MH_CreateHook(
            target,
            reinterpret_cast<void*>(&hooked_init),
            reinterpret_cast<void**>(&original)) != MH_OK) {
        error = "CGameScreen::Init hook creation failed";
        return false;
    }
    target_ = target;
    starting_ = std::move(starting);
    started_ = std::move(started);
    original_ = original;
    active_ = this;
    if (MH_EnableHook(target_) != MH_OK) {
        active_ = nullptr;
        original_ = nullptr;
        starting_ = {};
        started_ = {};
        (void)MH_RemoveHook(target_);
        target_ = nullptr;
        error = "CGameScreen::Init hook enable failed";
        return false;
    }
    installed_ = true;
    return true;
}

void MatchHook::remove() noexcept {
    if (!installed_) {
        return;
    }
    (void)MH_DisableHook(target_);
    active_ = nullptr;
    (void)MH_RemoveHook(target_);
    original_ = nullptr;
    starting_ = {};
    started_ = {};
    target_ = nullptr;
    installed_ = false;
}

bool MatchHook::installed() const noexcept {
    return installed_;
}

void __fastcall MatchHook::hooked_init(
    void* const instance,
    void*,
    void* const screen_data) noexcept {
    MatchHook* const active = active_;
    const InitFunction original = original_;
    if (active != nullptr && active->starting_) {
        try {
            active->starting_();
        } catch (...) {
            // Keep the game initialization boundary exception-safe.
        }
    }
    if (original != nullptr) {
        original(instance, screen_data);
    }
    if (active != nullptr && active->started_) {
        try {
            active->started_();
        } catch (...) {
            // Keep the game initialization boundary exception-safe.
        }
    }
}

}  // namespace btd5loader::runtime
