// SPDX-License-Identifier: GPL-3.0-only
#include "native_event_hook.hpp"

#include <MinHook.h>

#include <algorithm>
#include <utility>

namespace btd5loader::runtime {

NativeEventHook* NativeEventHook::active_{};
NativeEventHook::DispatchFunction NativeEventHook::original_{};

NativeEventHook::~NativeEventHook() {
    remove();
}

bool NativeEventHook::install(
    void* const dispatch_target,
    std::vector<NativeEventBinding> bindings,
    std::string& error) {
    if (installed_ || active_ != nullptr) {
        error = "native event hook is already installed";
        return false;
    }
    if (dispatch_target == nullptr || bindings.empty()) {
        error = "native event dispatch target or bindings are missing";
        return false;
    }
    for (auto binding = bindings.begin(); binding != bindings.end(); ++binding) {
        if (binding->event_vtable == nullptr || (!binding->before && !binding->after)) {
            error = "native event binding is incomplete";
            return false;
        }
        const bool duplicate = std::any_of(
            bindings.begin(),
            binding,
            [binding](const NativeEventBinding& candidate) {
                return candidate.event_vtable == binding->event_vtable;
            });
        if (duplicate) {
            error = "native event binding vtables must be unique";
            return false;
        }
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
    bindings_ = std::move(bindings);
    original_ = original;
    active_ = this;
    if (MH_EnableHook(dispatch_target_) != MH_OK) {
        active_ = nullptr;
        original_ = nullptr;
        bindings_.clear();
        (void)MH_RemoveHook(dispatch_target_);
        dispatch_target_ = nullptr;
        error = "CEventManager dispatch hook enable failed";
        return false;
    }
    installed_ = true;
    return true;
}

void NativeEventHook::remove() noexcept {
    if (!installed_) {
        return;
    }
    (void)MH_DisableHook(dispatch_target_);
    active_ = nullptr;
    (void)MH_RemoveHook(dispatch_target_);
    original_ = nullptr;
    bindings_.clear();
    dispatch_target_ = nullptr;
    installed_ = false;
}

bool NativeEventHook::installed() const noexcept {
    return installed_;
}

bool __fastcall NativeEventHook::hooked_dispatch(
    void* const manager,
    void*,
    void* const event,
    const bool queued) noexcept {
    NativeEventHook* const active = active_;
    const DispatchFunction original = original_;
    void* event_vtable = nullptr;
    if (event != nullptr) {
        event_vtable = *static_cast<void**>(event);
    }
    const NativeEventBinding* binding = nullptr;
    if (active != nullptr) {
        const auto found = std::find_if(
            active->bindings_.begin(),
            active->bindings_.end(),
            [event_vtable](const NativeEventBinding& candidate) {
                return candidate.event_vtable == event_vtable;
            });
        if (found != active->bindings_.end()) {
            binding = &*found;
        }
    }
    void* captured = nullptr;
    if (binding != nullptr && binding->capture) {
        try {
            captured = binding->capture(event);
        } catch (...) {
            // Treat failed payload capture as an unavailable optional payload.
        }
    }
    if (binding != nullptr && binding->before) {
        try {
            binding->before(captured);
        } catch (...) {
            // Keep the native event boundary exception-safe.
        }
    }
    const bool result = original != nullptr && original(manager, event, queued);
    if (binding != nullptr && binding->after) {
        try {
            binding->after(captured);
        } catch (...) {
            // The game may destroy the event during dispatch; never inspect it here.
        }
    }
    return result;
}

}  // namespace btd5loader::runtime
