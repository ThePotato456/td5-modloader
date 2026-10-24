// SPDX-License-Identifier: GPL-3.0-only
#include "hook_transaction.hpp"

#include <utility>

namespace btd5loader::runtime {

void HookTransaction::add(HookOperation operation) {
    if (!committed_) {
        operations_.push_back(std::move(operation));
    }
}

bool HookTransaction::commit(std::string& error) {
    if (committed_) {
        error = "hook transaction was already committed";
        return false;
    }
    for (std::size_t index = 0; index < operations_.size(); ++index) {
        auto& operation = operations_[index];
        if (!operation.install || !operation.remove) {
            if (!operation.required) {
                continue;
            }
            error = operation.symbol_name + ": hook callbacks are incomplete";
            rollback();
            return false;
        }
        if (!operation.install()) {
            if (!operation.required) {
                continue;
            }
            error = operation.symbol_name + ": hook installation failed";
            rollback();
            return false;
        }
        installed_.push_back(index);
    }
    committed_ = true;
    return true;
}

void HookTransaction::rollback() noexcept {
    for (auto iterator = installed_.rbegin(); iterator != installed_.rend(); ++iterator) {
        try {
            operations_[*iterator].remove();
        } catch (...) {
            // Keep unwinding so one broken cleanup cannot strand later hooks.
        }
    }
    installed_.clear();
    committed_ = false;
}

bool HookTransaction::committed() const noexcept {
    return committed_;
}

}  // namespace btd5loader::runtime
