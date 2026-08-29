// SPDX-License-Identifier: GPL-3.0-only
#include "lives_write_hook.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <utility>

namespace btd5loader::runtime {
void* g_lives_gain_continue{};
void* g_lives_loss_continue{};

void hooked_lives_gain_write() noexcept;
void hooked_lives_loss_write() noexcept;

namespace {

constexpr std::size_t kInstructionSize = 6;
constexpr std::size_t kLivesOffset = 0x88;
constexpr std::array<std::byte, kInstructionSize> kGainInstruction{
    std::byte{0x01}, std::byte{0x88}, std::byte{0x88},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
constexpr std::array<std::byte, kInstructionSize> kLossInstruction{
    std::byte{0x29}, std::byte{0x88}, std::byte{0x88},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};

bool write_jump(
    void* const target,
    void* const detour,
    const std::array<std::byte, kInstructionSize>& expected,
    std::string& error) {
    if (std::memcmp(target, expected.data(), expected.size()) != 0) {
        error = "lives write instruction validation failed";
        return false;
    }
    const auto source = reinterpret_cast<std::uintptr_t>(target);
    const auto destination = reinterpret_cast<std::uintptr_t>(detour);
    const auto displacement = static_cast<std::intptr_t>(destination - (source + 5));
    if (displacement < (std::numeric_limits<std::int32_t>::min)() ||
        displacement > (std::numeric_limits<std::int32_t>::max)()) {
        error = "lives write detour is outside the x86 jump range";
        return false;
    }
    DWORD previous_protection = 0;
    if (VirtualProtect(target, kInstructionSize, PAGE_EXECUTE_READWRITE, &previous_protection) ==
        FALSE) {
        error = "lives write instruction could not be made writable";
        return false;
    }
    std::array<std::byte, kInstructionSize> patch{
        std::byte{0xE9}, std::byte{}, std::byte{}, std::byte{}, std::byte{}, std::byte{0x90}};
    const auto relative = static_cast<std::int32_t>(displacement);
    std::memcpy(patch.data() + 1, &relative, sizeof(relative));
    std::memcpy(target, patch.data(), patch.size());
    FlushInstructionCache(GetCurrentProcess(), target, patch.size());
    DWORD ignored = 0;
    (void)VirtualProtect(target, kInstructionSize, previous_protection, &ignored);
    return true;
}

void restore_instruction(
    void* const target,
    const std::array<std::byte, kInstructionSize>& instruction) noexcept {
    if (target == nullptr) {
        return;
    }
    DWORD previous_protection = 0;
    if (VirtualProtect(target, kInstructionSize, PAGE_EXECUTE_READWRITE, &previous_protection) ==
        FALSE) {
        return;
    }
    std::memcpy(target, instruction.data(), instruction.size());
    FlushInstructionCache(GetCurrentProcess(), target, instruction.size());
    DWORD ignored = 0;
    (void)VirtualProtect(target, kInstructionSize, previous_protection, &ignored);
}

bool read_lives(void* const state, std::int32_t& lives) noexcept {
    if (state == nullptr) {
        return false;
    }
    SIZE_T bytes_read = 0;
    const auto* location = static_cast<const std::byte*>(state) + kLivesOffset;
    return ReadProcessMemory(
               GetCurrentProcess(), location, &lives, sizeof(lives), &bytes_read) != FALSE &&
           bytes_read == sizeof(lives);
}

}  // namespace

LivesWriteHook* LivesWriteHook::active_{};

LivesWriteHook::~LivesWriteHook() {
    remove();
}

bool LivesWriteHook::install(
    void* const gain_write_target,
    void* const loss_write_target,
    Callback changing,
    std::string& error) {
    if (installed_ || active_ != nullptr) {
        error = "lives write hook is already installed";
        return false;
    }
    if (gain_write_target == nullptr || loss_write_target == nullptr ||
        gain_write_target == loss_write_target || !changing) {
        error = "lives write hook target or callback is missing";
        return false;
    }
    gain_write_target_ = gain_write_target;
    loss_write_target_ = loss_write_target;
    g_lives_gain_continue = static_cast<std::byte*>(gain_write_target_) + kInstructionSize;
    g_lives_loss_continue = static_cast<std::byte*>(loss_write_target_) + kInstructionSize;
    changing_ = std::move(changing);
    active_ = this;
    if (!write_jump(gain_write_target_, &hooked_lives_gain_write, kGainInstruction, error)) {
        active_ = nullptr;
        changing_ = {};
        g_lives_gain_continue = nullptr;
        g_lives_loss_continue = nullptr;
        gain_write_target_ = nullptr;
        loss_write_target_ = nullptr;
        return false;
    }
    if (!write_jump(loss_write_target_, &hooked_lives_loss_write, kLossInstruction, error)) {
        restore_instruction(gain_write_target_, kGainInstruction);
        active_ = nullptr;
        changing_ = {};
        g_lives_gain_continue = nullptr;
        g_lives_loss_continue = nullptr;
        gain_write_target_ = nullptr;
        loss_write_target_ = nullptr;
        return false;
    }
    installed_ = true;
    return true;
}

void LivesWriteHook::remove() noexcept {
    if (!installed_) {
        return;
    }
    restore_instruction(loss_write_target_, kLossInstruction);
    restore_instruction(gain_write_target_, kGainInstruction);
    active_ = nullptr;
    changing_ = {};
    g_lives_gain_continue = nullptr;
    g_lives_loss_continue = nullptr;
    gain_write_target_ = nullptr;
    loss_write_target_ = nullptr;
    installed_ = false;
}

bool LivesWriteHook::installed() const noexcept {
    return installed_;
}

void __stdcall LivesWriteHook::dispatch_gain(
    void* const state,
    const std::int32_t amount) noexcept {
    std::int32_t before = 0;
    LivesWriteHook* const active = active_;
    if (active == nullptr || !active->changing_ || !read_lives(state, before)) {
        return;
    }
    const auto proposed = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(before) + static_cast<std::uint32_t>(amount));
    try {
        active->changing_(before, proposed);
    } catch (...) {
        // Keep the exact native lives write boundary exception-safe.
    }
}

void __stdcall LivesWriteHook::dispatch_loss(
    void* const state,
    const std::int32_t amount) noexcept {
    std::int32_t before = 0;
    LivesWriteHook* const active = active_;
    if (active == nullptr || !active->changing_ || !read_lives(state, before)) {
        return;
    }
    const std::int64_t difference =
        static_cast<std::int64_t>(before) - static_cast<std::int64_t>(amount);
    const auto proposed = static_cast<std::int32_t>(std::clamp<std::int64_t>(
        difference,
        0,
        (std::numeric_limits<std::int32_t>::max)()));
    try {
        active->changing_(before, proposed);
    } catch (...) {
        // Keep the exact native lives write boundary exception-safe.
    }
}

void __declspec(naked) hooked_lives_gain_write() noexcept {
    __asm {
        pushfd
        pushad
        push ecx
        push eax
        call LivesWriteHook::dispatch_gain
        popad
        popfd
        add dword ptr [eax + 88h], ecx
        jmp dword ptr [g_lives_gain_continue]
    }
}

void __declspec(naked) hooked_lives_loss_write() noexcept {
    __asm {
        pushfd
        pushad
        push ecx
        push eax
        call LivesWriteHook::dispatch_loss
        popad
        popfd
        sub dword ptr [eax + 88h], ecx
        jmp dword ptr [g_lives_loss_continue]
    }
}

}  // namespace btd5loader::runtime
