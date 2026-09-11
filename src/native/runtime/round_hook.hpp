// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <functional>
#include <string>

namespace btd5loader::runtime {

class RoundHook final {
public:
    using Callback = std::function<void(void*)>;

    ~RoundHook();
    RoundHook() = default;
    RoundHook(const RoundHook&) = delete;
    RoundHook& operator=(const RoundHook&) = delete;

    [[nodiscard]] bool install(void* target, Callback starting, std::string& error);
    void remove() noexcept;
    [[nodiscard]] bool installed() const noexcept;

private:
    using StartFunction = void(__thiscall*)(void*, void*);
    static void __fastcall hooked_start(void* round, void* register_padding, void* number) noexcept;

    static RoundHook* active_;
    static StartFunction original_;
    Callback starting_;
    void* target_{};
    bool installed_{};
};

}  // namespace btd5loader::runtime
