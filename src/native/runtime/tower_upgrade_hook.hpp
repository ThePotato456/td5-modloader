// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <string>

namespace btd5loader::runtime {

class TowerUpgradeHook final {
public:
    using Callback = std::function<bool(void*)>;

    ~TowerUpgradeHook();
    [[nodiscard]] bool install(void* target, Callback upgrading, std::string& error);
    void remove() noexcept;
    [[nodiscard]] bool installed() const noexcept;
    [[nodiscard]] static bool __stdcall dispatch(void* tower) noexcept;

private:
    static TowerUpgradeHook* active_;
    Callback upgrading_;
    std::array<std::byte, 8> original_{};
    void* target_{};
    bool installed_{};
};

}  // namespace btd5loader::runtime
