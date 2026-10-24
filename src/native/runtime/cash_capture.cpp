// SPDX-License-Identifier: GPL-3.0-only
#include "cash_capture.hpp"

#include <cmath>
#include <cstring>

namespace btd5loader::runtime {

void* CashCaptureStack::capture(const void* const event) noexcept {
    if (event == nullptr || depth_ >= values_.size()) {
        return nullptr;
    }
    double balance = 0.0;
    std::memcpy(
        &balance,
        static_cast<const unsigned char*>(event) + kBalanceOffset,
        sizeof(balance));
    if (!std::isfinite(balance)) {
        return nullptr;
    }
    auto& slot = values_[depth_++];
    slot = balance;
    return &slot;
}

std::optional<double> CashCaptureStack::value(const void* const token) const noexcept {
    if (token == nullptr || depth_ == 0 || token != &values_[depth_ - 1]) {
        return std::nullopt;
    }
    return values_[depth_ - 1];
}

void CashCaptureStack::release(const void* const token) noexcept {
    if (token != nullptr && depth_ != 0 && token == &values_[depth_ - 1]) {
        --depth_;
    }
}

std::size_t CashCaptureStack::depth() const noexcept {
    return depth_;
}

}  // namespace btd5loader::runtime
