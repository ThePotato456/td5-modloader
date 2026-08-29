// SPDX-License-Identifier: GPL-3.0-only
#include "lives_hook.hpp"

#include <Windows.h>

#include <MinHook.h>

#include <cstddef>
#include <utility>

namespace btd5loader::runtime {

LivesHook* LivesHook::active_{};
LivesHook::HandlerFunction LivesHook::original_gain_{};
LivesHook::HandlerFunction LivesHook::original_loss_{};

LivesHook::~LivesHook() {
    remove();
}

bool LivesHook::install(
    void* const gain_target,
    void* const loss_target,
    Callback changed,
    std::string& error) {
    if (installed_ || active_ != nullptr) {
        error = "lives hook is already installed";
        return false;
    }
    if (gain_target == nullptr || loss_target == nullptr || gain_target == loss_target || !changed) {
        error = "lives hook target or callback is missing";
        return false;
    }
    const MH_STATUS initialize_status = MH_Initialize();
    if (initialize_status != MH_OK && initialize_status != MH_ERROR_ALREADY_INITIALIZED) {
        error = "MinHook initialization failed";
        return false;
    }

    HandlerFunction original_gain = nullptr;
    if (MH_CreateHook(
            gain_target,
            reinterpret_cast<void*>(&hooked_gain),
            reinterpret_cast<void**>(&original_gain)) != MH_OK) {
        error = "give-lives handler hook creation failed";
        return false;
    }
    HandlerFunction original_loss = nullptr;
    if (MH_CreateHook(
            loss_target,
            reinterpret_cast<void*>(&hooked_loss),
            reinterpret_cast<void**>(&original_loss)) != MH_OK) {
        (void)MH_RemoveHook(gain_target);
        error = "escaped-bloon lives handler hook creation failed";
        return false;
    }

    gain_target_ = gain_target;
    loss_target_ = loss_target;
    changed_ = std::move(changed);
    original_gain_ = original_gain;
    original_loss_ = original_loss;
    active_ = this;
    if (MH_EnableHook(gain_target_) != MH_OK || MH_EnableHook(loss_target_) != MH_OK) {
        (void)MH_DisableHook(gain_target_);
        (void)MH_DisableHook(loss_target_);
        active_ = nullptr;
        original_gain_ = nullptr;
        original_loss_ = nullptr;
        changed_ = {};
        (void)MH_RemoveHook(loss_target_);
        (void)MH_RemoveHook(gain_target_);
        gain_target_ = nullptr;
        loss_target_ = nullptr;
        error = "lives handler hooks could not be enabled";
        return false;
    }
    installed_ = true;
    return true;
}

void LivesHook::remove() noexcept {
    if (!installed_) {
        return;
    }
    (void)MH_DisableHook(gain_target_);
    (void)MH_DisableHook(loss_target_);
    active_ = nullptr;
    (void)MH_RemoveHook(loss_target_);
    (void)MH_RemoveHook(gain_target_);
    original_gain_ = nullptr;
    original_loss_ = nullptr;
    changed_ = {};
    gain_target_ = nullptr;
    loss_target_ = nullptr;
    installed_ = false;
}

bool LivesHook::installed() const noexcept {
    return installed_;
}

void __fastcall LivesHook::hooked_gain(
    void* const observer,
    void*,
    void* const event,
    const bool queued) noexcept {
    const auto before = read_lives(observer, gain_state_pointer_offset);
    const HandlerFunction original = original_gain_;
    if (original != nullptr) {
        original(observer, event, queued);
    }
    dispatch_if_changed(before, read_lives(observer, gain_state_pointer_offset));
}

void __fastcall LivesHook::hooked_loss(
    void* const observer,
    void*,
    void* const event,
    const bool queued) noexcept {
    const auto before = read_lives(observer, loss_state_pointer_offset);
    const HandlerFunction original = original_loss_;
    if (original != nullptr) {
        original(observer, event, queued);
    }
    dispatch_if_changed(before, read_lives(observer, loss_state_pointer_offset));
}

std::optional<std::int32_t> LivesHook::read_lives(
    void* const observer,
    const std::size_t state_pointer_offset) noexcept {
    if (observer == nullptr) {
        return std::nullopt;
    }
    void* state = nullptr;
    SIZE_T bytes_read = 0;
    const auto* const state_location = static_cast<const std::byte*>(observer) + state_pointer_offset;
    if (ReadProcessMemory(
            GetCurrentProcess(), state_location, &state, sizeof(state), &bytes_read) == FALSE ||
        bytes_read != sizeof(state) || state == nullptr) {
        return std::nullopt;
    }
    std::int32_t lives = 0;
    const auto* const lives_location = static_cast<const std::byte*>(state) + lives_offset;
    if (ReadProcessMemory(
            GetCurrentProcess(), lives_location, &lives, sizeof(lives), &bytes_read) == FALSE ||
        bytes_read != sizeof(lives)) {
        return std::nullopt;
    }
    return lives;
}

void LivesHook::dispatch_if_changed(
    const std::optional<std::int32_t>& before,
    const std::optional<std::int32_t>& after) noexcept {
    LivesHook* const active = active_;
    if (active == nullptr || !active->changed_ || !before.has_value() || !after.has_value() ||
        before == after) {
        return;
    }
    try {
        active->changed_(*before, *after);
    } catch (...) {
        // Keep both native lives mutation boundaries exception-safe.
    }
}

}  // namespace btd5loader::runtime
