// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace btd5loader::runtime {

// Redirects the two purchase checks on the player's tower-placement path: the
// free-tower token check followed by the cost check. Both consume a resource
// when they succeed. A cancelled purchase skips both originals and reports
// failure, so the game takes its own unaffordable-placement rejection branch.
class TowerPurchaseHook final {
public:
    // Returns true to cancel the pending purchase.
    using Callback = std::function<bool(void*)>;

    TowerPurchaseHook() = default;
    ~TowerPurchaseHook();

    TowerPurchaseHook(const TowerPurchaseHook&) = delete;
    TowerPurchaseHook& operator=(const TowerPurchaseHook&) = delete;

    // `free_check_call` addresses the `call rel32` to the free-token check. The
    // cost-check call must follow at the fixed offset verified by install().
    [[nodiscard]] bool install(void* free_check_call, Callback purchasing, std::string& error);
    void remove() noexcept;
    [[nodiscard]] bool installed() const noexcept;

    static constexpr std::size_t kCostCheckCallOffset = 12;

private:
    using Check = std::uint32_t(__thiscall*)(void*, void*);

    static std::uint32_t __fastcall hooked_free_check(
        void* screen, void* register_padding, void* tower);
    static std::uint32_t __fastcall hooked_cost_check(
        void* screen, void* register_padding, void* tower);

    static TowerPurchaseHook* active_;
    static Check original_free_check_;
    static Check original_cost_check_;

    Callback purchasing_;
    std::byte* free_call_{};
    std::byte* cost_call_{};
    std::array<std::byte, 5> free_original_{};
    std::array<std::byte, 5> cost_original_{};
    void* cancelled_tower_{};
    bool installed_{};
};

}  // namespace btd5loader::runtime
