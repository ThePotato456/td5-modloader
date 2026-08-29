// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <functional>
#include <string>

namespace btd5loader::runtime {

class TowerSaleHook final {
public:
    using Callback = std::function<void(void*)>;

    ~TowerSaleHook();
    [[nodiscard]] bool install(void* target, Callback selling, std::string& error);
    void remove() noexcept;
    [[nodiscard]] bool installed() const noexcept;
    static void __stdcall dispatch(void* tower) noexcept;

private:
    static TowerSaleHook* active_;
    Callback selling_;
    void* target_{};
    bool installed_{};
};

}  // namespace btd5loader::runtime
