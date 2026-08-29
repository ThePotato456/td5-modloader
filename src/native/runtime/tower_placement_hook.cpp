// SPDX-License-Identifier: GPL-3.0-only
#include "tower_placement_hook.hpp"

#include <MinHook.h>

#include <utility>

namespace btd5loader::runtime {

TowerPlacementHook* TowerPlacementHook::active_{};
TowerPlacementHook::PlacementFunction TowerPlacementHook::original_{};

TowerPlacementHook::~TowerPlacementHook() {
    remove();
}

bool TowerPlacementHook::install(
    void* const target,
    Callback placing,
    std::string& error) {
    if (installed_ || active_ != nullptr) {
        error = "tower placement hook is already installed";
        return false;
    }
    if (target == nullptr || !placing) {
        error = "tower placement hook target or callback is missing";
        return false;
    }
    const MH_STATUS initialize_status = MH_Initialize();
    if (initialize_status != MH_OK && initialize_status != MH_ERROR_ALREADY_INITIALIZED) {
        error = "MinHook initialization failed";
        return false;
    }

    PlacementFunction original = nullptr;
    if (MH_CreateHook(
            target,
            reinterpret_cast<void*>(&hooked_placement),
            reinterpret_cast<void**>(&original)) != MH_OK) {
        error = "tower placement hook creation failed";
        return false;
    }
    target_ = target;
    placing_ = std::move(placing);
    original_ = original;
    active_ = this;
    if (MH_EnableHook(target_) != MH_OK) {
        active_ = nullptr;
        original_ = nullptr;
        placing_ = {};
        (void)MH_RemoveHook(target_);
        target_ = nullptr;
        error = "tower placement hook enable failed";
        return false;
    }
    installed_ = true;
    return true;
}

void TowerPlacementHook::remove() noexcept {
    if (!installed_) {
        return;
    }
    (void)MH_DisableHook(target_);
    active_ = nullptr;
    (void)MH_RemoveHook(target_);
    original_ = nullptr;
    placing_ = {};
    target_ = nullptr;
    installed_ = false;
}

bool TowerPlacementHook::installed() const noexcept {
    return installed_;
}

void __fastcall TowerPlacementHook::hooked_placement(
    void* const manager,
    void*,
    void* const tower) noexcept {
    TowerPlacementHook* const active = active_;
    const PlacementFunction original = original_;
    if (active != nullptr && active->placing_) {
        try {
            active->placing_(tower);
        } catch (...) {
            // Keep the native placement boundary exception-safe.
        }
    }
    if (original != nullptr) {
        original(manager, tower);
    }
}

}  // namespace btd5loader::runtime
