// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <Windows.h>

#include <atomic>
#include <functional>
#include <string>

namespace btd5loader::runtime {

class FrameHook final {
public:
    using Callback = std::function<void(HDC)>;

    FrameHook() = default;
    ~FrameHook();

    FrameHook(const FrameHook&) = delete;
    FrameHook& operator=(const FrameHook&) = delete;

    [[nodiscard]] bool install(Callback callback, std::string& error);
    void remove() noexcept;
    [[nodiscard]] bool installed() const noexcept;

private:
    using SwapBuffersFunction = BOOL(WINAPI*)(HDC);

    static BOOL WINAPI hooked_swap_buffers(HDC device_context) noexcept;

    static std::atomic<FrameHook*> active_;
    static std::atomic<SwapBuffersFunction> original_;

    Callback callback_;
    void* target_{};
    bool installed_{};
};

}  // namespace btd5loader::runtime
