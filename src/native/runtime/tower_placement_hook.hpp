// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <functional>
#include <string>

namespace btd5loader::runtime {

class TowerPlacementHook final {
public:
    using Callback = std::function<void(void*)>;

    TowerPlacementHook() = default;
    ~TowerPlacementHook();

    TowerPlacementHook(const TowerPlacementHook&) = delete;
    TowerPlacementHook& operator=(const TowerPlacementHook&) = delete;

    [[nodiscard]] bool install(void* target, Callback placing, std::string& error);
    void remove() noexcept;
    [[nodiscard]] bool installed() const noexcept;

private:
    using PlacementFunction = void(__thiscall*)(void*, void*);

    static void __fastcall hooked_placement(
        void* manager,
        void* register_padding,
        void* tower) noexcept;

    static TowerPlacementHook* active_;
    static PlacementFunction original_;

    Callback placing_;
    void* target_{};
    bool installed_{};
};

}  // namespace btd5loader::runtime
