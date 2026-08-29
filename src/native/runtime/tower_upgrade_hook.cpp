// SPDX-License-Identifier: GPL-3.0-only
#include "tower_upgrade_hook.hpp"

#include <Windows.h>

#include <cstring>
#include <limits>
#include <utility>

namespace btd5loader::runtime {
void* g_tower_upgrade_continue{};
void* g_tower_upgrade_commit{};
void hooked_tower_upgrade() noexcept;

TowerUpgradeHook* TowerUpgradeHook::active_{};

TowerUpgradeHook::~TowerUpgradeHook() {
    remove();
}

bool TowerUpgradeHook::install(void* const target, Callback upgrading, std::string& error) {
    if (installed_ || active_ != nullptr) {
        error = "tower upgrade hook is already installed";
        return false;
    }
    if (target == nullptr || !upgrading) {
        error = "tower upgrade hook target or callback is missing";
        return false;
    }
    std::memcpy(original_.data(), target, original_.size());
    const auto* const bytes = static_cast<const std::byte*>(target);
    if (bytes[0] != std::byte{0x56} || bytes[1] != std::byte{0x8B} ||
        bytes[2] != std::byte{0xCF} || bytes[3] != std::byte{0xE8}) {
        error = "tower upgrade commit instruction validation failed";
        return false;
    }
    std::int32_t call_displacement = 0;
    std::memcpy(&call_displacement, bytes + 4, sizeof(call_displacement));
    g_tower_upgrade_commit = const_cast<std::byte*>(bytes) + 8 + call_displacement;
    g_tower_upgrade_continue = static_cast<std::byte*>(target) + original_.size();
    const auto source = reinterpret_cast<std::uintptr_t>(target);
    const auto destination = reinterpret_cast<std::uintptr_t>(&hooked_tower_upgrade);
    const auto displacement = static_cast<std::intptr_t>(destination - (source + 5));
    if (displacement < (std::numeric_limits<std::int32_t>::min)() ||
        displacement > (std::numeric_limits<std::int32_t>::max)()) {
        error = "tower upgrade detour is outside the x86 jump range";
        return false;
    }
    DWORD protection = 0;
    if (VirtualProtect(target, original_.size(), PAGE_EXECUTE_READWRITE, &protection) == FALSE) {
        error = "tower upgrade instructions could not be made writable";
        return false;
    }
    std::array<std::byte, 8> patch{
        std::byte{0xE9}, {}, {}, {}, {}, std::byte{0x90}, std::byte{0x90}, std::byte{0x90}};
    const auto relative = static_cast<std::int32_t>(displacement);
    std::memcpy(patch.data() + 1, &relative, sizeof(relative));
    std::memcpy(target, patch.data(), patch.size());
    FlushInstructionCache(GetCurrentProcess(), target, patch.size());
    DWORD ignored = 0;
    (void)VirtualProtect(target, original_.size(), protection, &ignored);
    upgrading_ = std::move(upgrading);
    target_ = target;
    active_ = this;
    installed_ = true;
    return true;
}

void TowerUpgradeHook::remove() noexcept {
    if (!installed_) return;
    DWORD protection = 0;
    if (VirtualProtect(target_, original_.size(), PAGE_EXECUTE_READWRITE, &protection) != FALSE) {
        std::memcpy(target_, original_.data(), original_.size());
        FlushInstructionCache(GetCurrentProcess(), target_, original_.size());
        DWORD ignored = 0;
        (void)VirtualProtect(target_, original_.size(), protection, &ignored);
    }
    active_ = nullptr;
    upgrading_ = {};
    target_ = nullptr;
    g_tower_upgrade_continue = nullptr;
    g_tower_upgrade_commit = nullptr;
    installed_ = false;
}

bool TowerUpgradeHook::installed() const noexcept { return installed_; }

void __stdcall TowerUpgradeHook::dispatch(void* const tower) noexcept {
    auto* const active = active_;
    if (active != nullptr && active->upgrading_) {
        try { active->upgrading_(tower); } catch (...) {}
    }
}

void __declspec(naked) hooked_tower_upgrade() noexcept {
    __asm {
        pushfd
        pushad
        push edi
        call TowerUpgradeHook::dispatch
        popad
        popfd
        push esi
        mov ecx, edi
        call dword ptr [g_tower_upgrade_commit]
        jmp dword ptr [g_tower_upgrade_continue]
    }
}

}  // namespace btd5loader::runtime
