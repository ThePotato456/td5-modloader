// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace btd5loader::runtime {

struct HookOperation final {
    std::string symbol_name;
    bool required{true};
    std::function<bool()> install;
    std::function<void()> remove;
};

class HookTransaction final {
public:
    void add(HookOperation operation);
    [[nodiscard]] bool commit(std::string& error);
    void rollback() noexcept;
    [[nodiscard]] bool committed() const noexcept;

private:
    std::vector<HookOperation> operations_;
    std::vector<std::size_t> installed_;
    bool committed_{};
};

}  // namespace btd5loader::runtime
