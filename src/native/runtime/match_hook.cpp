// SPDX-License-Identifier: GPL-3.0-only
#include "match_hook.hpp"

#include <MinHook.h>

#include <utility>

namespace btd5loader::runtime {

MatchHook* MatchHook::active_{};
MatchHook::InitFunction MatchHook::original_init_{};
MatchHook::UninitFunction MatchHook::original_uninit_{};

MatchHook::~MatchHook() {
    remove();
}

bool MatchHook::install(
    void* const init_target,
    void* const uninit_target,
    Callback starting,
    Callback started,
    Callback ending,
    Callback ended,
    std::string& error) {
    if (installed_ || active_ != nullptr) {
        error = "match hook is already installed";
        return false;
    }
    if (init_target == nullptr || uninit_target == nullptr || !starting || !started || !ending ||
        !ended) {
        error = "match hook target or callback is missing";
        return false;
    }
    const MH_STATUS initialize_status = MH_Initialize();
    if (initialize_status != MH_OK && initialize_status != MH_ERROR_ALREADY_INITIALIZED) {
        error = "MinHook initialization failed";
        return false;
    }

    InitFunction original_init = nullptr;
    if (MH_CreateHook(
            init_target,
            reinterpret_cast<void*>(&hooked_init),
            reinterpret_cast<void**>(&original_init)) != MH_OK) {
        error = "CGameScreen::Init hook creation failed";
        return false;
    }
    UninitFunction original_uninit = nullptr;
    if (MH_CreateHook(
            uninit_target,
            reinterpret_cast<void*>(&hooked_uninit),
            reinterpret_cast<void**>(&original_uninit)) != MH_OK) {
        (void)MH_RemoveHook(init_target);
        error = "CGameScreen::Uninit hook creation failed";
        return false;
    }
    init_target_ = init_target;
    uninit_target_ = uninit_target;
    starting_ = std::move(starting);
    started_ = std::move(started);
    ending_ = std::move(ending);
    ended_ = std::move(ended);
    original_init_ = original_init;
    original_uninit_ = original_uninit;
    active_ = this;
    if (MH_EnableHook(init_target_) != MH_OK || MH_EnableHook(uninit_target_) != MH_OK) {
        (void)MH_DisableHook(init_target_);
        (void)MH_DisableHook(uninit_target_);
        active_ = nullptr;
        original_init_ = nullptr;
        original_uninit_ = nullptr;
        starting_ = {};
        started_ = {};
        ending_ = {};
        ended_ = {};
        (void)MH_RemoveHook(uninit_target_);
        (void)MH_RemoveHook(init_target_);
        init_target_ = nullptr;
        uninit_target_ = nullptr;
        error = "CGameScreen lifecycle hook enable failed";
        return false;
    }
    installed_ = true;
    return true;
}

void MatchHook::remove() noexcept {
    if (!installed_) {
        return;
    }
    (void)MH_DisableHook(init_target_);
    (void)MH_DisableHook(uninit_target_);
    active_ = nullptr;
    (void)MH_RemoveHook(uninit_target_);
    (void)MH_RemoveHook(init_target_);
    original_init_ = nullptr;
    original_uninit_ = nullptr;
    starting_ = {};
    started_ = {};
    ending_ = {};
    ended_ = {};
    init_target_ = nullptr;
    uninit_target_ = nullptr;
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
    const InitFunction original = original_init_;
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

void __fastcall MatchHook::hooked_uninit(void* const instance, void*) noexcept {
    MatchHook* const active = active_;
    const UninitFunction original = original_uninit_;
    if (active != nullptr && active->ending_) {
        try {
            active->ending_();
        } catch (...) {
            // Keep the game teardown boundary exception-safe.
        }
    }
    if (original != nullptr) {
        original(instance);
    }
    if (active != nullptr && active->ended_) {
        try {
            active->ended_();
        } catch (...) {
            // Keep the game teardown boundary exception-safe.
        }
    }
}

}  // namespace btd5loader::runtime
