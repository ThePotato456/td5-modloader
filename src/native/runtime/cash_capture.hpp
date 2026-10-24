// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <optional>

namespace btd5loader::runtime {

// Copies the post-write balance carried by a native `CMoneyUpdatedEvent`
// (a `double` at byte offset 8) before dispatch, because the game may destroy
// the event during dispatch. Nested money updates raised by native observers
// get their own slot; each capture is released by its matching dispatch end.
class CashCaptureStack final {
public:
    static constexpr std::size_t kBalanceOffset = 8;
    static constexpr std::size_t kCapacity = 16;

    // Returns an opaque token, or nullptr when the event is missing, the
    // balance is not finite, or nesting exceeds the bounded capacity.
    [[nodiscard]] void* capture(const void* event) noexcept;
    [[nodiscard]] std::optional<double> value(const void* token) const noexcept;
    void release(const void* token) noexcept;
    [[nodiscard]] std::size_t depth() const noexcept;

private:
    std::array<double, kCapacity> values_{};
    std::size_t depth_{};
};

}  // namespace btd5loader::runtime
