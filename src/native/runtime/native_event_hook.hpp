// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace btd5loader::runtime {

struct NativeEventBinding final {
    void* event_vtable{};
    std::function<void()> before;
    std::function<void()> after;
};

class NativeEventHook final {
public:
    NativeEventHook() = default;
    ~NativeEventHook();

    NativeEventHook(const NativeEventHook&) = delete;
    NativeEventHook& operator=(const NativeEventHook&) = delete;

    [[nodiscard]] bool install(
        void* dispatch_target,
        std::vector<NativeEventBinding> bindings,
        std::string& error);
    void remove() noexcept;
    [[nodiscard]] bool installed() const noexcept;

private:
    using DispatchFunction = bool(__thiscall*)(void*, void*, bool);

    static bool __fastcall hooked_dispatch(
        void* manager,
        void* register_padding,
        void* event,
        bool queued) noexcept;

    static NativeEventHook* active_;
    static DispatchFunction original_;

    std::vector<NativeEventBinding> bindings_;
    void* dispatch_target_{};
    bool installed_{};
};

}  // namespace btd5loader::runtime
