// SPDX-License-Identifier: GPL-3.0-only
#include "tower_purchase_hook.hpp"

#include <Windows.h>

#include <cstring>
#include <limits>
#include <utility>

namespace btd5loader::runtime {
namespace {

std::byte* call_target(const std::byte* const call) noexcept {
    std::int32_t displacement = 0;
    std::memcpy(&displacement, call + 1, sizeof(displacement));
    return const_cast<std::byte*>(call) + 5 + displacement;
}

bool write_call(std::byte* const call, const void* const destination) noexcept {
    const auto source = reinterpret_cast<std::uintptr_t>(call);
    const auto target = reinterpret_cast<std::uintptr_t>(destination);
    const auto displacement = static_cast<std::intptr_t>(target - (source + 5));
    if (displacement < (std::numeric_limits<std::int32_t>::min)() ||
        displacement > (std::numeric_limits<std::int32_t>::max)()) {
        return false;
    }
    DWORD protection = 0;
    if (VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &protection) == FALSE) {
        return false;
    }
    const auto relative = static_cast<std::int32_t>(displacement);
    call[0] = std::byte{0xE8};
    std::memcpy(call + 1, &relative, sizeof(relative));
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    DWORD ignored = 0;
    (void)VirtualProtect(call, 5, protection, &ignored);
    return true;
}

void restore(std::byte* const call, const std::array<std::byte, 5>& original) noexcept {
    if (call == nullptr) {
        return;
    }
    DWORD protection = 0;
    if (VirtualProtect(call, original.size(), PAGE_EXECUTE_READWRITE, &protection) == FALSE) {
        return;
    }
    std::memcpy(call, original.data(), original.size());
    FlushInstructionCache(GetCurrentProcess(), call, original.size());
    DWORD ignored = 0;
    (void)VirtualProtect(call, original.size(), protection, &ignored);
}

}  // namespace

TowerPurchaseHook* TowerPurchaseHook::active_{};
TowerPurchaseHook::Check TowerPurchaseHook::original_free_check_{};
TowerPurchaseHook::Check TowerPurchaseHook::original_cost_check_{};

TowerPurchaseHook::~TowerPurchaseHook() {
    remove();
}

bool TowerPurchaseHook::install(
    void* const free_check_call,
    Callback purchasing,
    std::string& error) {
    if (installed_ || active_ != nullptr) {
        error = "tower purchase hook is already installed";
        return false;
    }
    if (free_check_call == nullptr || !purchasing) {
        error = "tower purchase hook target or callback is missing";
        return false;
    }
    auto* const free_call = static_cast<std::byte*>(free_check_call);
    auto* const cost_call = free_call + kCostCheckCallOffset;
    // call free; test al,al; jne commit; push edi; mov ecx,ebx; call cost; test al,al; jne commit
    const bool shape =
        free_call[0] == std::byte{0xE8} && free_call[5] == std::byte{0x84} &&
        free_call[6] == std::byte{0xC0} && free_call[7] == std::byte{0x75} &&
        free_call[9] == std::byte{0x57} && free_call[10] == std::byte{0x8B} &&
        free_call[11] == std::byte{0xCB} && cost_call[0] == std::byte{0xE8} &&
        cost_call[5] == std::byte{0x84} && cost_call[6] == std::byte{0xC0} &&
        cost_call[7] == std::byte{0x75};
    if (!shape) {
        error = "tower purchase check instruction validation failed";
        return false;
    }
    // Both successful checks must branch to the same commit path.
    const auto* const free_commit = free_call + 9 + static_cast<std::int8_t>(free_call[8]);
    const auto* const cost_commit = cost_call + 9 + static_cast<std::int8_t>(cost_call[8]);
    if (free_commit != cost_commit) {
        error = "tower purchase commit branch validation failed";
        return false;
    }

    std::memcpy(free_original_.data(), free_call, free_original_.size());
    std::memcpy(cost_original_.data(), cost_call, cost_original_.size());
    original_free_check_ = reinterpret_cast<Check>(call_target(free_call));
    original_cost_check_ = reinterpret_cast<Check>(call_target(cost_call));
    purchasing_ = std::move(purchasing);
    free_call_ = free_call;
    cost_call_ = cost_call;
    active_ = this;
    // Redirect the cost check first so a cancellation recorded by the free
    // check is always consumed by the matching redirected cost check.
    if (!write_call(cost_call_, reinterpret_cast<const void*>(&hooked_cost_check)) ||
        !write_call(free_call_, reinterpret_cast<const void*>(&hooked_free_check))) {
        installed_ = true;
        remove();
        error = "tower purchase checks could not be redirected";
        return false;
    }
    installed_ = true;
    return true;
}

void TowerPurchaseHook::remove() noexcept {
    if (!installed_) {
        return;
    }
    restore(free_call_, free_original_);
    restore(cost_call_, cost_original_);
    if (active_ == this) {
        active_ = nullptr;
    }
    original_free_check_ = nullptr;
    original_cost_check_ = nullptr;
    purchasing_ = {};
    free_call_ = nullptr;
    cost_call_ = nullptr;
    cancelled_tower_ = nullptr;
    installed_ = false;
}

bool TowerPurchaseHook::installed() const noexcept {
    return installed_;
}

std::uint32_t __fastcall TowerPurchaseHook::hooked_free_check(
    void* const screen,
    void*,
    void* const tower) {
    TowerPurchaseHook* const active = active_;
    bool cancelled = false;
    if (active != nullptr && active->purchasing_) {
        try {
            cancelled = active->purchasing_(tower);
        } catch (...) {
            // A failed dispatch never cancels the native purchase.
            cancelled = false;
        }
        active->cancelled_tower_ = cancelled ? tower : nullptr;
    }
    if (cancelled) {
        return 0;
    }
    return original_free_check_(screen, tower);
}

std::uint32_t __fastcall TowerPurchaseHook::hooked_cost_check(
    void* const screen,
    void*,
    void* const tower) {
    TowerPurchaseHook* const active = active_;
    if (active != nullptr && active->cancelled_tower_ != nullptr) {
        const bool cancelled = active->cancelled_tower_ == tower;
        active->cancelled_tower_ = nullptr;
        if (cancelled) {
            return 0;
        }
    }
    return original_cost_check_(screen, tower);
}

}  // namespace btd5loader::runtime
