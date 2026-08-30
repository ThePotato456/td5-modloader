// SPDX-License-Identifier: GPL-3.0-only
#include "tower_sale_hook.hpp"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <utility>

namespace btd5loader::runtime {
void* g_tower_sale_continue{};
void* g_tower_sale_cancel{};
void hooked_tower_sale() noexcept;

namespace {
constexpr std::array<std::byte, 6> kOriginal{
    std::byte{0x8D}, std::byte{0xBB}, std::byte{0x14},
    std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};

void restore(void* const target) noexcept {
    if (target == nullptr) return;
    DWORD protection = 0;
    if (VirtualProtect(target, kOriginal.size(), PAGE_EXECUTE_READWRITE, &protection) == FALSE) return;
    std::memcpy(target, kOriginal.data(), kOriginal.size());
    FlushInstructionCache(GetCurrentProcess(), target, kOriginal.size());
    DWORD ignored = 0;
    (void)VirtualProtect(target, kOriginal.size(), protection, &ignored);
}
}  // namespace

TowerSaleHook* TowerSaleHook::active_{};
TowerSaleHook::~TowerSaleHook() { remove(); }

bool TowerSaleHook::install(void* const target, Callback selling, std::string& error) {
    if (installed_ || active_ != nullptr) {
        error = "tower sale hook is already installed";
        return false;
    }
    if (target == nullptr || !selling || std::memcmp(target, kOriginal.data(), kOriginal.size()) != 0) {
        error = "tower sale hook target, callback, or instruction validation failed";
        return false;
    }
    const auto* const bytes = static_cast<const std::byte*>(target);
    const auto* const eligibility = bytes - 8;
    if (eligibility[0] != std::byte{0x84} || eligibility[1] != std::byte{0xC0} ||
        eligibility[2] != std::byte{0x0F} || eligibility[3] != std::byte{0x85}) {
        error = "tower sale rejection branch validation failed";
        return false;
    }
    std::int32_t rejection_displacement = 0;
    std::memcpy(&rejection_displacement, eligibility + 4, sizeof(rejection_displacement));
    g_tower_sale_cancel = const_cast<std::byte*>(bytes) + rejection_displacement;
    const auto source = reinterpret_cast<std::uintptr_t>(target);
    const auto destination = reinterpret_cast<std::uintptr_t>(&hooked_tower_sale);
    const auto displacement = static_cast<std::intptr_t>(destination - (source + 5));
    if (displacement < (std::numeric_limits<std::int32_t>::min)() ||
        displacement > (std::numeric_limits<std::int32_t>::max)()) {
        error = "tower sale detour is outside the x86 jump range";
        return false;
    }
    DWORD protection = 0;
    if (VirtualProtect(target, kOriginal.size(), PAGE_EXECUTE_READWRITE, &protection) == FALSE) {
        error = "tower sale instruction could not be made writable";
        return false;
    }
    std::array<std::byte, 6> patch{
        std::byte{0xE9}, {}, {}, {}, {}, std::byte{0x90}};
    const auto relative = static_cast<std::int32_t>(displacement);
    std::memcpy(patch.data() + 1, &relative, sizeof(relative));
    std::memcpy(target, patch.data(), patch.size());
    FlushInstructionCache(GetCurrentProcess(), target, patch.size());
    DWORD ignored = 0;
    (void)VirtualProtect(target, patch.size(), protection, &ignored);
    target_ = target;
    selling_ = std::move(selling);
    g_tower_sale_continue = static_cast<std::byte*>(target) + kOriginal.size();
    active_ = this;
    installed_ = true;
    return true;
}

void TowerSaleHook::remove() noexcept {
    if (!installed_) return;
    restore(target_);
    active_ = nullptr;
    selling_ = {};
    target_ = nullptr;
    g_tower_sale_continue = nullptr;
    g_tower_sale_cancel = nullptr;
    installed_ = false;
}

bool TowerSaleHook::installed() const noexcept { return installed_; }

bool __stdcall TowerSaleHook::dispatch(void* const tower) noexcept {
    auto* const active = active_;
    if (active != nullptr && active->selling_) {
        try {
            return active->selling_(tower);
        } catch (...) {
        }
    }
    return false;
}

void __declspec(naked) hooked_tower_sale() noexcept {
    __asm {
        pushfd
        pushad
        mov eax, dword ptr [ebx - 1Ch]
        push eax
        call TowerSaleHook::dispatch
        mov dword ptr [esp + 28], eax
        popad
        popfd
        test al, al
        jne cancelled
        lea edi, [ebx - 0ECh]
        jmp dword ptr [g_tower_sale_continue]
    cancelled:
        jmp dword ptr [g_tower_sale_cancel]
    }
}

}  // namespace btd5loader::runtime
