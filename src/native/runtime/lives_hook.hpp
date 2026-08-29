// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace btd5loader::runtime {

class LivesHook final {
public:
    using Callback = std::function<void(std::int32_t, std::int32_t)>;

    LivesHook() = default;
    ~LivesHook();

    LivesHook(const LivesHook&) = delete;
    LivesHook& operator=(const LivesHook&) = delete;

    [[nodiscard]] bool install(
        void* gain_target,
        void* loss_target,
        Callback changed,
        std::string& error);
    void remove() noexcept;
    [[nodiscard]] bool installed() const noexcept;

private:
    using HandlerFunction = void(__thiscall*)(void*, void*, bool);

    static void __fastcall hooked_gain(
        void* observer,
        void* register_padding,
        void* event,
        bool queued) noexcept;
    static void __fastcall hooked_loss(
        void* observer,
        void* register_padding,
        void* event,
        bool queued) noexcept;
    [[nodiscard]] static std::optional<std::int32_t> read_lives(
        void* observer,
        std::size_t state_pointer_offset) noexcept;
    static void dispatch_if_changed(
        const std::optional<std::int32_t>& before,
        const std::optional<std::int32_t>& after) noexcept;

    static constexpr std::size_t gain_state_pointer_offset = 0x40C;
    static constexpr std::size_t loss_state_pointer_offset = 0x42C;
    static constexpr std::size_t lives_offset = 0x88;

    static LivesHook* active_;
    static HandlerFunction original_gain_;
    static HandlerFunction original_loss_;

    Callback changed_;
    void* gain_target_{};
    void* loss_target_{};
    bool installed_{};
};

}  // namespace btd5loader::runtime
