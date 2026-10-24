// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <string>

namespace btd5loader::runtime {

// Replaces the verified CRT predicate call immediately after game.main returns.
class ExitHook final {
public:
    using Predicate = bool(__cdecl*)();
    using Callback = std::function<void()>;
    ~ExitHook();
    [[nodiscard]] bool install(void* call, Predicate predicate, Callback callback, std::string& error);
    void remove() noexcept;
private:
    static bool __cdecl dispatch() noexcept;
    static ExitHook* active_;
    void* call_{};
    Predicate predicate_{};
    Callback callback_;
    std::array<std::byte, 5> original_{};
};

}  // namespace btd5loader::runtime
