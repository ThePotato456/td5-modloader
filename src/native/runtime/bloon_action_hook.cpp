// SPDX-License-Identifier: GPL-3.0-only
#include "bloon_action_hook.hpp"

#include <Windows.h>

#include <MinHook.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

namespace btd5loader::runtime {
void* g_bloon_leak_allocate{};
void* g_bloon_leak_continue{};
void* g_bloon_leak_cancel{};
void hooked_bloon_leak() noexcept;

namespace {
void invoke(const BloonActionHook::Callback& callback, void* const bloon) noexcept {
    if (callback) {
        try {
            callback(bloon);
        } catch (...) {
            // Keep the native bloon action boundary exception-safe.
        }
    }
}

void restore(void* const target, const std::array<std::byte, 7>& original) noexcept {
    if (target == nullptr) {
        return;
    }
    DWORD protection = 0;
    if (VirtualProtect(target, original.size(), PAGE_EXECUTE_READWRITE, &protection) == FALSE) {
        return;
    }
    std::memcpy(target, original.data(), original.size());
    FlushInstructionCache(GetCurrentProcess(), target, original.size());
    DWORD ignored = 0;
    (void)VirtualProtect(target, original.size(), protection, &ignored);
}
}  // namespace

BloonActionHook* BloonActionHook::active_{};
BloonActionHook::SpawnFunction BloonActionHook::original_primary_spawn_{};
BloonActionHook::SpawnFunction BloonActionHook::original_secondary_spawn_{};
BloonActionHook::PopFunction BloonActionHook::original_pop_{};

BloonActionHook::~BloonActionHook() {
    remove();
}

bool BloonActionHook::install(
    void* const primary_spawn,
    void* const secondary_spawn,
    void* const pop_commit,
    void* const leak_commit,
    Callback spawning,
    Callback popping,
    CancellableCallback leaking,
    std::string& error) {
    if (installed_ || active_ != nullptr) {
        error = "bloon action hook is already installed";
        return false;
    }
    if (primary_spawn == nullptr || secondary_spawn == nullptr || pop_commit == nullptr ||
        leak_commit == nullptr || !spawning || !popping || !leaking) {
        error = "bloon action hook target or callback is missing";
        return false;
    }

    std::memcpy(leak_original_.data(), leak_commit, leak_original_.size());
    if (leak_original_[0] != std::byte{0x6A} || leak_original_[1] != std::byte{0x08} ||
        leak_original_[2] != std::byte{0xE8}) {
        error = "bloon leak commit instruction validation failed";
        return false;
    }
    const auto* const leak_bytes = static_cast<const std::byte*>(leak_commit);
    const auto* const track_end_branch = leak_bytes - 6;
    if (track_end_branch[0] != std::byte{0x0F} ||
        track_end_branch[1] != std::byte{0x82}) {
        error = "bloon leak alternate-path branch validation failed";
        return false;
    }
    std::int32_t cancel_displacement = 0;
    std::memcpy(&cancel_displacement, track_end_branch + 2, sizeof(cancel_displacement));
    g_bloon_leak_cancel = const_cast<std::byte*>(leak_bytes) + cancel_displacement;
    std::int32_t allocate_displacement = 0;
    std::memcpy(&allocate_displacement, leak_original_.data() + 3, sizeof(allocate_displacement));
    g_bloon_leak_allocate = static_cast<std::byte*>(leak_commit) + 7 + allocate_displacement;
    g_bloon_leak_continue = static_cast<std::byte*>(leak_commit) + leak_original_.size();

    const auto source = reinterpret_cast<std::uintptr_t>(leak_commit);
    const auto destination = reinterpret_cast<std::uintptr_t>(&hooked_bloon_leak);
    const auto displacement = static_cast<std::intptr_t>(destination - (source + 5));
    if (displacement < (std::numeric_limits<std::int32_t>::min)() ||
        displacement > (std::numeric_limits<std::int32_t>::max)()) {
        error = "bloon leak detour is outside the x86 jump range";
        return false;
    }

    const MH_STATUS initialize_status = MH_Initialize();
    if (initialize_status != MH_OK && initialize_status != MH_ERROR_ALREADY_INITIALIZED) {
        error = "MinHook initialization failed";
        return false;
    }
    SpawnFunction primary_original = nullptr;
    SpawnFunction secondary_original = nullptr;
    PopFunction pop_original = nullptr;
    if (MH_CreateHook(
            primary_spawn,
            reinterpret_cast<void*>(&hooked_primary_spawn),
            reinterpret_cast<void**>(&primary_original)) != MH_OK ||
        MH_CreateHook(
            secondary_spawn,
            reinterpret_cast<void*>(&hooked_secondary_spawn),
            reinterpret_cast<void**>(&secondary_original)) != MH_OK ||
        MH_CreateHook(
            pop_commit,
            reinterpret_cast<void*>(&hooked_pop),
            reinterpret_cast<void**>(&pop_original)) != MH_OK) {
        (void)MH_RemoveHook(primary_spawn);
        (void)MH_RemoveHook(secondary_spawn);
        (void)MH_RemoveHook(pop_commit);
        error = "bloon spawn or pop hook creation failed";
        return false;
    }

    primary_spawn_ = primary_spawn;
    secondary_spawn_ = secondary_spawn;
    pop_commit_ = pop_commit;
    leak_commit_ = leak_commit;
    spawning_ = std::move(spawning);
    popping_ = std::move(popping);
    leaking_ = std::move(leaking);
    original_primary_spawn_ = primary_original;
    original_secondary_spawn_ = secondary_original;
    original_pop_ = pop_original;
    active_ = this;

    if (MH_EnableHook(primary_spawn_) != MH_OK || MH_EnableHook(secondary_spawn_) != MH_OK ||
        MH_EnableHook(pop_commit_) != MH_OK) {
        remove();
        error = "bloon spawn or pop hook enable failed";
        return false;
    }

    DWORD protection = 0;
    if (VirtualProtect(
            leak_commit_, leak_original_.size(), PAGE_EXECUTE_READWRITE, &protection) == FALSE) {
        remove();
        error = "bloon leak commit instruction could not be made writable";
        return false;
    }
    std::array<std::byte, 7> patch{
        std::byte{0xE9}, {}, {}, {}, {}, std::byte{0x90}, std::byte{0x90}};
    const auto relative = static_cast<std::int32_t>(displacement);
    std::memcpy(patch.data() + 1, &relative, sizeof(relative));
    std::memcpy(leak_commit_, patch.data(), patch.size());
    FlushInstructionCache(GetCurrentProcess(), leak_commit_, patch.size());
    DWORD ignored = 0;
    (void)VirtualProtect(leak_commit_, patch.size(), protection, &ignored);
    installed_ = true;
    return true;
}

void BloonActionHook::remove() noexcept {
    if (leak_commit_ != nullptr) {
        restore(leak_commit_, leak_original_);
    }
    if (primary_spawn_ != nullptr) {
        (void)MH_DisableHook(primary_spawn_);
        (void)MH_RemoveHook(primary_spawn_);
    }
    if (secondary_spawn_ != nullptr) {
        (void)MH_DisableHook(secondary_spawn_);
        (void)MH_RemoveHook(secondary_spawn_);
    }
    if (pop_commit_ != nullptr) {
        (void)MH_DisableHook(pop_commit_);
        (void)MH_RemoveHook(pop_commit_);
    }
    active_ = nullptr;
    original_primary_spawn_ = nullptr;
    original_secondary_spawn_ = nullptr;
    original_pop_ = nullptr;
    spawning_ = {};
    popping_ = {};
    leaking_ = {};
    primary_spawn_ = nullptr;
    secondary_spawn_ = nullptr;
    pop_commit_ = nullptr;
    leak_commit_ = nullptr;
    g_bloon_leak_allocate = nullptr;
    g_bloon_leak_continue = nullptr;
    g_bloon_leak_cancel = nullptr;
    installed_ = false;
}

bool BloonActionHook::installed() const noexcept {
    return installed_;
}

void __fastcall BloonActionHook::hooked_primary_spawn(
    void* const manager,
    void*,
    void* const bloon) noexcept {
    BloonActionHook* const active = active_;
    const SpawnFunction original = original_primary_spawn_;
    if (active != nullptr) {
        invoke(active->spawning_, bloon);
    }
    if (original != nullptr) {
        original(manager, bloon);
    }
}

void __fastcall BloonActionHook::hooked_secondary_spawn(
    void* const manager,
    void*,
    void* const bloon) noexcept {
    BloonActionHook* const active = active_;
    const SpawnFunction original = original_secondary_spawn_;
    if (active != nullptr) {
        invoke(active->spawning_, bloon);
    }
    if (original != nullptr) {
        original(manager, bloon);
    }
}

void __fastcall BloonActionHook::hooked_pop(
    void* const manager,
    void*,
    void* const bloon,
    void* const child_layers,
    const bool forced) noexcept {
    BloonActionHook* const active = active_;
    const PopFunction original = original_pop_;
    if (active != nullptr) {
        invoke(active->popping_, bloon);
    }
    if (original != nullptr) {
        original(manager, bloon, child_layers, forced);
    }
}

bool __stdcall BloonActionHook::dispatch_leaking(void* const bloon) noexcept {
    BloonActionHook* const active = active_;
    if (active != nullptr && active->leaking_) {
        try {
            return active->leaking_(bloon);
        } catch (...) {
        }
    }
    return false;
}

void __declspec(naked) hooked_bloon_leak() noexcept {
    __asm {
        pushfd
        pushad
        push ebx
        call BloonActionHook::dispatch_leaking
        mov dword ptr [esp + 28], eax
        popad
        popfd
        test al, al
        jne cancelled
        push 8
        call dword ptr [g_bloon_leak_allocate]
        jmp dword ptr [g_bloon_leak_continue]
    cancelled:
        jmp dword ptr [g_bloon_leak_cancel]
    }
}

}  // namespace btd5loader::runtime
