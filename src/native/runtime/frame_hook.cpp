// SPDX-License-Identifier: GPL-3.0-only
#include "frame_hook.hpp"

#include <MinHook.h>

#include <utility>

namespace btd5loader::runtime {

std::atomic<FrameHook*> FrameHook::active_{};
std::atomic<FrameHook::SwapBuffersFunction> FrameHook::original_{};

FrameHook::~FrameHook() {
    remove();
}

bool FrameHook::install(Callback callback, std::string& error) {
    if (installed_) {
        error = "frame hook is already installed";
        return false;
    }
    if (!callback) {
        error = "frame hook callback is missing";
        return false;
    }

    const HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
    target_ = gdi == nullptr
                  ? nullptr
                  : reinterpret_cast<void*>(GetProcAddress(gdi, "SwapBuffers"));
    if (target_ == nullptr) {
        error = "GDI32!SwapBuffers is unavailable";
        return false;
    }

    const MH_STATUS initialize_status = MH_Initialize();
    if (initialize_status != MH_OK && initialize_status != MH_ERROR_ALREADY_INITIALIZED) {
        error = "MinHook initialization failed";
        target_ = nullptr;
        return false;
    }

    SwapBuffersFunction original = nullptr;
    if (MH_CreateHook(
            target_,
            reinterpret_cast<void*>(&hooked_swap_buffers),
            reinterpret_cast<void**>(&original)) != MH_OK) {
        error = "SwapBuffers hook creation failed";
        target_ = nullptr;
        return false;
    }

    callback_ = std::move(callback);
    original_.store(original, std::memory_order_release);
    active_.store(this, std::memory_order_release);
    if (MH_EnableHook(target_) != MH_OK) {
        active_.store(nullptr, std::memory_order_release);
        original_.store(nullptr, std::memory_order_release);
        callback_ = {};
        (void)MH_RemoveHook(target_);
        target_ = nullptr;
        error = "SwapBuffers hook enable failed";
        return false;
    }
    installed_ = true;
    return true;
}

void FrameHook::remove() noexcept {
    if (!installed_) {
        return;
    }
    (void)MH_DisableHook(target_);
    active_.store(nullptr, std::memory_order_release);
    (void)MH_RemoveHook(target_);
    original_.store(nullptr, std::memory_order_release);
    callback_ = {};
    target_ = nullptr;
    installed_ = false;
}

bool FrameHook::installed() const noexcept {
    return installed_;
}

BOOL WINAPI FrameHook::hooked_swap_buffers(const HDC device_context) noexcept {
    FrameHook* const active = active_.load(std::memory_order_acquire);
    if (active != nullptr && active->callback_) {
        try {
            active->callback_(device_context);
        } catch (...) {
            // Never allow a mod-host exception to cross the Win32 rendering boundary.
        }
    }
    const SwapBuffersFunction original = original_.load(std::memory_order_acquire);
    return original == nullptr ? FALSE : original(device_context);
}

}  // namespace btd5loader::runtime
