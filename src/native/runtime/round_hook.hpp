// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <functional>
#include <string>

namespace btd5loader::runtime {

class RoundHook final {
public:
    using Callback = std::function<void()>;

    RoundHook() = default;
    ~RoundHook();

    RoundHook(const RoundHook&) = delete;
    RoundHook& operator=(const RoundHook&) = delete;

    [[nodiscard]] bool install(
        void* dispatch_target,
        void* started_vtable,
        void* ended_vtable,
        Callback starting,
        Callback started,
        Callback ending,
        Callback ended,
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

    static RoundHook* active_;
    static DispatchFunction original_;

    Callback starting_;
    Callback started_;
    Callback ending_;
    Callback ended_;
    void* dispatch_target_{};
    void* started_vtable_{};
    void* ended_vtable_{};
    bool installed_{};
};

}  // namespace btd5loader::runtime
