// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <functional>
#include <string>

namespace btd5loader::runtime {

class MatchHook final {
public:
    using Callback = std::function<void()>;

    MatchHook() = default;
    ~MatchHook();

    MatchHook(const MatchHook&) = delete;
    MatchHook& operator=(const MatchHook&) = delete;

    [[nodiscard]] bool install(
        void* init_target,
        void* uninit_target,
        Callback starting,
        Callback started,
        Callback ending,
        Callback ended,
        std::string& error);
    void remove() noexcept;
    [[nodiscard]] bool installed() const noexcept;

private:
    using InitFunction = void(__thiscall*)(void*, void*);
    using UninitFunction = void(__thiscall*)(void*);

    static void __fastcall hooked_init(void* instance, void* register_padding, void* screen_data) noexcept;
    static void __fastcall hooked_uninit(void* instance, void* register_padding) noexcept;

    static MatchHook* active_;
    static InitFunction original_init_;
    static UninitFunction original_uninit_;

    Callback starting_;
    Callback started_;
    Callback ending_;
    Callback ended_;
    void* init_target_{};
    void* uninit_target_{};
    bool installed_{};
};

}  // namespace btd5loader::runtime
