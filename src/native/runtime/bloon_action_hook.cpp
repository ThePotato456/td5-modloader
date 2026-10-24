// SPDX-License-Identifier: GPL-3.0-only
#include "bloon_action_hook.hpp"

#include <Windows.h>
#include <intrin.h>

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
void* g_bloon_lethal_continue{};
void hooked_bloon_leak() noexcept;
void hooked_bloon_lethal_hit() noexcept;

namespace {
// CBloon flags set by every native removal: popping and removed.
constexpr std::size_t kBloonPoppingFlag = 0x220;
constexpr std::size_t kBloonRemovedFlag = 0x222;
// CBloonManager's deferred-deletion queue (std::vector<CBloon*>), drained and
// destroyed at the start of the next manager update.
constexpr std::size_t kManagerDeletionQueue = 0x187C;

// Primary spawn: lea ecx,[edi+4] / mov [ebp+8],ebx / call push_back.
constexpr std::size_t kSpawnPushCall = 0x31;
constexpr std::array<std::byte, 7> kSpawnPushSite{
    std::byte{0x8D}, std::byte{0x4F}, std::byte{0x04}, std::byte{0x89}, std::byte{0x5D},
    std::byte{0x08}, std::byte{0xE8}};
constexpr std::array<std::byte, 6> kQueuePushPrologue{
    std::byte{0x55}, std::byte{0x8B}, std::byte{0xEC}, std::byte{0x56}, std::byte{0x8B},
    std::byte{0xF1}};
// Pop routine: push 1 / mov ecx,edi / call strip_effects on the parent.
constexpr std::size_t kPopStripCall = 0x14D;
constexpr std::array<std::byte, 5> kPopStripSite{
    std::byte{0x6A}, std::byte{0x01}, std::byte{0x8B}, std::byte{0xCF}, std::byte{0xE8}};
constexpr std::array<std::byte, 5> kStripEffectsPrologue{
    std::byte{0x55}, std::byte{0x8B}, std::byte{0xEC}, std::byte{0x6A}, std::byte{0xFF}};
// The emitter's virtual spawn call: mov eax,[ecx] / call [eax+4].
constexpr std::array<std::byte, 5> kEmitterSpawnCall{
    std::byte{0x8B}, std::byte{0x01}, std::byte{0xFF}, std::byte{0x50}, std::byte{0x04}};

template<std::size_t Size>
[[nodiscard]] bool bytes_match(
    const std::byte* const at, const std::array<std::byte, Size>& expected) noexcept {
    return std::memcmp(at, expected.data(), Size) == 0;
}

// Target of the E8 call that ends the Size-byte site starting at `site`.
template<std::size_t Size>
[[nodiscard]] const std::byte* call_target(const std::byte* const site) noexcept {
    std::int32_t relative = 0;
    std::memcpy(&relative, site + Size, sizeof(relative));
    return site + Size + sizeof(relative) + relative;
}

// The lethal-hit commit: movss [edi+0x224], xmm0 / jb +7 / mov byte [edi+0x220], 1.
constexpr std::array<std::byte, 17> kLethalHitInstructions{
    std::byte{0xF3}, std::byte{0x0F}, std::byte{0x11}, std::byte{0x87}, std::byte{0x24},
    std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x72}, std::byte{0x07},
    std::byte{0xC6}, std::byte{0x87}, std::byte{0x20}, std::byte{0x02}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x01}};

[[nodiscard]] bool jump_displacement(
    const void* const source, const void* const destination, std::int32_t& relative) noexcept {
    const auto displacement = static_cast<std::intptr_t>(
        reinterpret_cast<std::uintptr_t>(destination) -
        (reinterpret_cast<std::uintptr_t>(source) + 5));
    if (displacement < (std::numeric_limits<std::int32_t>::min)() ||
        displacement > (std::numeric_limits<std::int32_t>::max)()) {
        return false;
    }
    relative = static_cast<std::int32_t>(displacement);
    return true;
}

// Writes a JMP to destination and pads the rest of the patch with NOPs.
template<std::size_t Size>
[[nodiscard]] bool write_jump(
    void* const target, const std::int32_t relative) noexcept {
    DWORD protection = 0;
    if (VirtualProtect(target, Size, PAGE_EXECUTE_READWRITE, &protection) == FALSE) return false;
    std::array<std::byte, Size> patch{};
    patch.fill(std::byte{0x90});
    patch[0] = std::byte{0xE9};
    std::memcpy(patch.data() + 1, &relative, sizeof(relative));
    std::memcpy(target, patch.data(), patch.size());
    FlushInstructionCache(GetCurrentProcess(), target, patch.size());
    DWORD ignored = 0;
    (void)VirtualProtect(target, Size, protection, &ignored);
    return true;
}

template<std::size_t Size>
void restore(void* const target, const std::array<std::byte, Size>& original) noexcept {
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

BloonActionHook::~BloonActionHook() {
    remove();
}

bool BloonActionHook::resolve_rejection(
    const void* const primary_spawn,
    const void* const pop_routine,
    void* const emitter_return,
    SpawnRejection& rejection,
    std::string& error) noexcept {
    rejection = {};
    if (primary_spawn == nullptr || pop_routine == nullptr || emitter_return == nullptr) {
        error = "bloon spawn rejection anchor is missing";
        return false;
    }
    const auto* const emitter = static_cast<const std::byte*>(emitter_return);
    if (!bytes_match(emitter - kEmitterSpawnCall.size(), kEmitterSpawnCall)) {
        error = "bloon emitter spawn call validation failed";
        return false;
    }
    const auto* const push_site = static_cast<const std::byte*>(primary_spawn) + kSpawnPushCall;
    if (!bytes_match(push_site, kSpawnPushSite)) {
        error = "bloon spawn push_back call validation failed";
        return false;
    }
    const auto* const queue_push = call_target<kSpawnPushSite.size()>(push_site);
    if (!bytes_match(queue_push, kQueuePushPrologue)) {
        error = "bloon queue push_back prologue validation failed";
        return false;
    }
    const auto* const strip_site = static_cast<const std::byte*>(pop_routine) + kPopStripCall;
    if (!bytes_match(strip_site, kPopStripSite)) {
        error = "bloon effect removal call validation failed";
        return false;
    }
    const auto* const strip_effects = call_target<kPopStripSite.size()>(strip_site);
    if (!bytes_match(strip_effects, kStripEffectsPrologue)) {
        error = "bloon effect removal prologue validation failed";
        return false;
    }
    rejection.emitter_return = emitter_return;
    rejection.queue_push = const_cast<std::byte*>(queue_push);
    rejection.strip_effects = const_cast<std::byte*>(strip_effects);
    return true;
}

bool BloonActionHook::reject_spawn(void* const manager, void* const bloon) const noexcept {
    // begin, end, capacity of the deletion queue.
    std::array<std::uintptr_t, 3> queue{};
    std::memcpy(queue.data(), static_cast<const std::byte*>(manager) + kManagerDeletionQueue,
                sizeof(queue));
    if (queue[0] > queue[1] || queue[1] > queue[2] ||
        (queue[1] - queue[0]) % sizeof(void*) != 0 ||
        (queue[2] - queue[0]) % sizeof(void*) != 0 || (queue[0] == 0) != (queue[2] == 0)) {
        return false;
    }
    auto* const bytes = static_cast<std::byte*>(bloon);
    bytes[kBloonPoppingFlag] = std::byte{1};
    bytes[kBloonRemovedFlag] = std::byte{1};
    reinterpret_cast<StripEffectsFunction>(rejection_.strip_effects)(bloon, 0);
    void* const value = bloon;
    reinterpret_cast<QueuePushFunction>(rejection_.queue_push)(
        static_cast<std::byte*>(manager) + kManagerDeletionQueue, &value);
    return true;
}

bool BloonActionHook::install(
    void* const primary_spawn,
    void* const secondary_spawn,
    const SpawnRejection& rejection,
    void* const lethal_hit,
    void* const leak_commit,
    SpawnCallback spawning,
    CancellableCallback popping,
    CancellableCallback leaking,
    std::string& error) {
    if (installed_ || active_ != nullptr) {
        error = "bloon action hook is already installed";
        return false;
    }
    if (primary_spawn == nullptr || secondary_spawn == nullptr || lethal_hit == nullptr ||
        leak_commit == nullptr || rejection.emitter_return == nullptr ||
        rejection.queue_push == nullptr || rejection.strip_effects == nullptr || !spawning ||
        !popping || !leaking) {
        error = "bloon action hook target or callback is missing";
        return false;
    }

    std::memcpy(lethal_original_.data(), lethal_hit, lethal_original_.size());
    if (lethal_original_ != kLethalHitInstructions) {
        error = "bloon lethal-hit commit instruction validation failed";
        return false;
    }
    std::int32_t lethal_relative = 0;
    if (!jump_displacement(lethal_hit, reinterpret_cast<const void*>(&hooked_bloon_lethal_hit),
                           lethal_relative)) {
        error = "bloon lethal-hit detour is outside the x86 jump range";
        return false;
    }
    g_bloon_lethal_continue = static_cast<std::byte*>(lethal_hit) + lethal_original_.size();

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

    std::int32_t leak_relative = 0;
    if (!jump_displacement(leak_commit, reinterpret_cast<const void*>(&hooked_bloon_leak),
                           leak_relative)) {
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
    if (MH_CreateHook(
            primary_spawn,
            reinterpret_cast<void*>(&hooked_primary_spawn),
            reinterpret_cast<void**>(&primary_original)) != MH_OK ||
        MH_CreateHook(
            secondary_spawn,
            reinterpret_cast<void*>(&hooked_secondary_spawn),
            reinterpret_cast<void**>(&secondary_original)) != MH_OK) {
        (void)MH_RemoveHook(primary_spawn);
        (void)MH_RemoveHook(secondary_spawn);
        error = "bloon spawn hook creation failed";
        return false;
    }

    primary_spawn_ = primary_spawn;
    secondary_spawn_ = secondary_spawn;
    leak_commit_ = leak_commit;
    spawning_ = std::move(spawning);
    rejection_ = rejection;
    popping_ = std::move(popping);
    leaking_ = std::move(leaking);
    original_primary_spawn_ = primary_original;
    original_secondary_spawn_ = secondary_original;
    active_ = this;

    if (MH_EnableHook(primary_spawn_) != MH_OK || MH_EnableHook(secondary_spawn_) != MH_OK) {
        remove();
        error = "bloon spawn hook enable failed";
        return false;
    }
    if (!write_jump<7>(leak_commit_, leak_relative)) {
        remove();
        error = "bloon leak commit instruction could not be made writable";
        return false;
    }
    lethal_hit_ = lethal_hit;
    if (!write_jump<17>(lethal_hit_, lethal_relative)) {
        remove();
        error = "bloon lethal-hit commit instruction could not be made writable";
        return false;
    }
    installed_ = true;
    return true;
}

void BloonActionHook::remove() noexcept {
    if (lethal_hit_ != nullptr) {
        restore(lethal_hit_, lethal_original_);
    }
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
    active_ = nullptr;
    original_primary_spawn_ = nullptr;
    original_secondary_spawn_ = nullptr;
    spawning_ = {};
    rejection_ = {};
    popping_ = {};
    leaking_ = {};
    primary_spawn_ = nullptr;
    secondary_spawn_ = nullptr;
    lethal_hit_ = nullptr;
    leak_commit_ = nullptr;
    g_bloon_leak_allocate = nullptr;
    g_bloon_leak_continue = nullptr;
    g_bloon_leak_cancel = nullptr;
    g_bloon_lethal_continue = nullptr;
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
    // Only the round emitter is verified not to use the bloon after spawning it.
    if (active != nullptr) {
        const bool cancellable = _ReturnAddress() == active->rejection_.emitter_return;
        if (dispatch_spawning(bloon, cancellable) && cancellable &&
            active->reject_spawn(manager, bloon)) {
            return;
        }
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
    // The boss spawner keeps and reads the bloon after this call.
    if (active != nullptr) {
        (void)dispatch_spawning(bloon, false);
    }
    if (original != nullptr) {
        original(manager, bloon);
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

// Kept out of line: a handler exception thrown inside the inlined MinHook
// detour frame terminated the process instead of reaching this catch.
__declspec(noinline) bool __stdcall BloonActionHook::dispatch_spawning(void* const bloon, const bool cancellable) noexcept {
    BloonActionHook* const active = active_;
    if (active != nullptr && active->spawning_) {
        try {
            return active->spawning_(bloon, cancellable);
        } catch (...) {
            // Keep the native bloon action boundary exception-safe.
        }
    }
    return false;
}

bool __stdcall BloonActionHook::dispatch_popping(void* const bloon) noexcept {
    BloonActionHook* const active = active_;
    if (active != nullptr && active->popping_) {
        try {
            return active->popping_(bloon);
        } catch (...) {
        }
    }
    return false;
}

// Replaces the damage function's health commit and pop-flag store. Flags still
// hold the comiss of 0 against the new health (EDI = bloon, XMM0 = new health,
// memory = pre-hit health). Only a first lethal hit dispatches; a cancelled one
// leaves the pre-hit health in place and reloads it into XMM0.
void __declspec(naked) hooked_bloon_lethal_hit() noexcept {
    __asm {
        jb alive
        cmp byte ptr [edi + 0x220], 0
        jne commit_pop
        lea esp, [esp - 64]
        movdqu [esp], xmm0
        movdqu [esp + 16], xmm1
        movdqu [esp + 32], xmm2
        movdqu [esp + 48], xmm3
        push 0
        pushad
        push edi
        call BloonActionHook::dispatch_popping
        mov dword ptr [esp + 32], eax
        popad
        cmp byte ptr [esp], 0
        lea esp, [esp + 4]
        movdqu xmm0, [esp]
        movdqu xmm1, [esp + 16]
        movdqu xmm2, [esp + 32]
        movdqu xmm3, [esp + 48]
        lea esp, [esp + 64]
        jne cancelled
    commit_pop:
        movss dword ptr [edi + 0x224], xmm0
        mov byte ptr [edi + 0x220], 1
        jmp dword ptr [g_bloon_lethal_continue]
    cancelled:
        movss xmm0, dword ptr [edi + 0x224]
        jmp dword ptr [g_bloon_lethal_continue]
    alive:
        movss dword ptr [edi + 0x224], xmm0
        jmp dword ptr [g_bloon_lethal_continue]
    }
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
