// SPDX-License-Identifier: GPL-3.0-only
#include "exit_hook.hpp"

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <utility>

namespace btd5loader::runtime {
namespace {
std::mutex exit_mutex;
ExitHook::Predicate last_predicate{};
}
ExitHook* ExitHook::active_{};

ExitHook::~ExitHook() { remove(); }

bool ExitHook::install(void* call, Predicate predicate, Callback callback, std::string& error) {
    std::scoped_lock lock(exit_mutex);
    if (active_ != nullptr || call == nullptr || predicate == nullptr || !callback) {
        error = "exit hook target or callback is unavailable";
        return false;
    }
    if ((reinterpret_cast<std::uintptr_t>(call) + 1) % alignof(LONG) != 0) {
        error = "exit call displacement must be aligned for atomic replacement";
        return false;
    }
    std::array<std::byte, 5> bytes{};
    std::memcpy(bytes.data(), call, bytes.size());
    std::int32_t displacement{};
    std::memcpy(&displacement, bytes.data() + 1, sizeof(displacement));
    if (bytes[0] != std::byte{0xE8} ||
        static_cast<std::byte*>(call) + 5 + displacement != reinterpret_cast<void*>(predicate)) {
        error = "exit predicate call validation failed";
        return false;
    }
    DWORD protection{};
    if (!VirtualProtect(call, bytes.size(), PAGE_EXECUTE_READWRITE, &protection)) {
        error = "exit predicate call could not be made writable";
        return false;
    }
    original_ = bytes;
    call_ = call;
    predicate_ = predicate;
    last_predicate = predicate;
    callback_ = std::move(callback);
    active_ = this;
    const auto relative = static_cast<std::int32_t>(
        reinterpret_cast<std::uintptr_t>(&dispatch) - (reinterpret_cast<std::uintptr_t>(call) + 5));
    InterlockedExchange(reinterpret_cast<volatile LONG*>(static_cast<std::byte*>(call) + 1), relative);
    FlushInstructionCache(GetCurrentProcess(), call, bytes.size());
    DWORD ignored{};
    (void)VirtualProtect(call, bytes.size(), protection, &ignored);
    return true;
}

void ExitHook::remove() noexcept {
    std::scoped_lock lock(exit_mutex);
    if (call_ == nullptr) return;
    DWORD protection{};
    if (!VirtualProtect(call_, original_.size(), PAGE_EXECUTE_READWRITE, &protection)) return;
    LONG relative{};
    std::memcpy(&relative, original_.data() + 1, sizeof(relative));
    InterlockedExchange(reinterpret_cast<volatile LONG*>(static_cast<std::byte*>(call_) + 1), relative);
    FlushInstructionCache(GetCurrentProcess(), call_, original_.size());
    DWORD ignored{};
    (void)VirtualProtect(call_, original_.size(), protection, &ignored);
    call_ = nullptr;
    active_ = nullptr;
    callback_ = {};
    predicate_ = nullptr;
}

bool __cdecl ExitHook::dispatch() noexcept {
    Predicate predicate{};
    Callback callback;
    try {
        {
            std::scoped_lock lock(exit_mutex);
            // A thread may already have fetched the old CALL during removal.
            predicate = last_predicate;
            if (active_ != nullptr) callback = active_->callback_;
        }
        // Do not hold the hook lock while shutdown waits for the startup worker.
        if (callback) callback();
    } catch (...) {
        // Always preserve the CRT continuation even if loader cleanup fails.
    }
    return predicate != nullptr && predicate();
}

}  // namespace btd5loader::runtime
