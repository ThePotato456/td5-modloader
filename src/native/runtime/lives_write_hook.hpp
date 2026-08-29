// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace btd5loader::runtime {

class LivesWriteHook final {
public:
    using Callback = std::function<bool(std::int32_t, std::int32_t)>;

    LivesWriteHook() = default;
    ~LivesWriteHook();

    LivesWriteHook(const LivesWriteHook&) = delete;
    LivesWriteHook& operator=(const LivesWriteHook&) = delete;

    [[nodiscard]] bool install(
        void* gain_write_target,
        void* loss_write_target,
        Callback changing,
        std::string& error);
    void remove() noexcept;
    [[nodiscard]] bool installed() const noexcept;

    static int __stdcall dispatch_gain(void* state, std::int32_t amount) noexcept;
    static int __stdcall dispatch_loss(void* state, std::int32_t amount) noexcept;

private:
    static LivesWriteHook* active_;

    Callback changing_;
    void* gain_write_target_{};
    void* loss_write_target_{};
    bool installed_{};
};

}  // namespace btd5loader::runtime
