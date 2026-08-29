// SPDX-License-Identifier: GPL-3.0-only
#include "round_hook.hpp"

#include <MinHook.h>

#include <utility>

namespace btd5loader::runtime {

RoundHook* RoundHook::active_{};
RoundHook::DispatchFunction RoundHook::original_{};

RoundHook::~RoundHook() {
    remove();
}

bool RoundHook::install(
    void* const dispatch_target,
    void* const started_vtable,
    void* const ended_vtable,
    Callback starting,
    Callback started,
    Callback ending,
    Callback ended,
    std::string& error) {
    if (installed_ || active_ != nullptr) {
        error = "round hook is already installed";
        return false;
    }
    if (dispatch_target == nullptr || started_vtable == nullptr || ended_vtable == nullptr ||
        !starting || !started || !ending || !ended) {
        error = "round hook target, event type, or callback is missing";
        return false;
    }
    const MH_STATUS initialize_status = MH_Initialize();
    if (initialize_status != MH_OK && initialize_status != MH_ERROR_ALREADY_INITIALIZED) {
        error = "MinHook initialization failed";
        return false;
    }

    DispatchFunction original = nullptr;
    if (MH_CreateHook(
            dispatch_target,
            reinterpret_cast<void*>(&hooked_dispatch),
            reinterpret_cast<void**>(&original)) != MH_OK) {
        error = "CEventManager dispatch hook creation failed";
        return false;
    }
    dispatch_target_ = dispatch_target;
    started_vtable_ = started_vtable;
    ended_vtable_ = ended_vtable;
    starting_ = std::move(starting);
    started_ = std::move(started);
    ending_ = std::move(ending);
    ended_ = std::move(ended);
    original_ = original;
    active_ = this;
    if (MH_EnableHook(dispatch_target_) != MH_OK) {
        active_ = nullptr;
        original_ = nullptr;
        starting_ = {};
        started_ = {};
        ending_ = {};
        ended_ = {};
        started_vtable_ = nullptr;
        ended_vtable_ = nullptr;
        (void)MH_RemoveHook(dispatch_target_);
        dispatch_target_ = nullptr;
        error = "CEventManager dispatch hook enable failed";
        return false;
    }
    installed_ = true;
    return true;
}

void RoundHook::remove() noexcept {
    if (!installed_) {
        return;
    }
    (void)MH_DisableHook(dispatch_target_);
    active_ = nullptr;
    (void)MH_RemoveHook(dispatch_target_);
    original_ = nullptr;
    starting_ = {};
    started_ = {};
    ending_ = {};
    ended_ = {};
    dispatch_target_ = nullptr;
    started_vtable_ = nullptr;
    ended_vtable_ = nullptr;
    installed_ = false;
}

bool RoundHook::installed() const noexcept {
    return installed_;
}

bool __fastcall RoundHook::hooked_dispatch(
    void* const manager,
    void*,
    void* const event,
    const bool queued) noexcept {
    RoundHook* const active = active_;
    const DispatchFunction original = original_;
    void* event_vtable = nullptr;
    if (event != nullptr) {
        event_vtable = *static_cast<void**>(event);
    }
    Callback* before = nullptr;
    Callback* after = nullptr;
    if (active != nullptr && event_vtable == active->started_vtable_) {
        before = &active->starting_;
        after = &active->started_;
    } else if (active != nullptr && event_vtable == active->ended_vtable_) {
        before = &active->ending_;
        after = &active->ended_;
    }
    if (before != nullptr && *before) {
        try {
            (*before)();
        } catch (...) {
            // Keep the native event boundary exception-safe.
        }
    }
    const bool result = original != nullptr && original(manager, event, queued);
    if (after != nullptr && *after) {
        try {
            (*after)();
        } catch (...) {
            // The game may destroy the event during dispatch; never inspect it here.
        }
    }
    return result;
}

}  // namespace btd5loader::runtime
