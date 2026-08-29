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
        void* target,
        Callback starting,
        Callback started,
        std::string& error);
    void remove() noexcept;
    [[nodiscard]] bool installed() const noexcept;

private:
    using InitFunction = void(__thiscall*)(void*, void*);

    static void __fastcall hooked_init(void* instance, void* register_padding, void* screen_data) noexcept;

    static MatchHook* active_;
    static InitFunction original_;

    Callback starting_;
    Callback started_;
    void* target_{};
    bool installed_{};
};

}  // namespace btd5loader::runtime
