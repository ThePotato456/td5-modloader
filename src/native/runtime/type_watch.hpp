// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <string>
#include <vector>

namespace btd5loader::runtime {

// Research diagnostic for native tower-type reads and writes; inert unless the
// BTD5ML_DIAG_TYPE_WATCH environment variable is set. It puts hardware
// read/write watchpoints on the first placed tower's 64-bit type flag (+0xD0
// and +0xD4) in every thread that exists when it arms, and counts each
// instruction that touches either half. A watchpoint traps after the access,
// so each record names the address of the following instruction ("after")
// and the return address one EBP frame up ("frame"); frame is only meaningful
// when the reading function keeps a frame pointer. When the game copies the
// type into a popped bloon (+0x250), the remaining two debug registers follow
// that latest copy on the copying thread; those records have half bits 2/3.
class TypeWatch final {
public:
    TypeWatch() = default;
    ~TypeWatch();

    TypeWatch(const TypeWatch&) = delete;
    TypeWatch& operator=(const TypeWatch&) = delete;

    // Arms on `tower` unless disabled or already armed; returns a log line
    // describing the result, or an empty string when nothing happened.
    [[nodiscard]] std::string arm(void* tower);

    // Disarms when `tower` is the watched one, or always when it is null, and
    // returns the recorded summary lines (empty when nothing was armed).
    [[nodiscard]] std::vector<std::string> disarm(const void* tower);

    // Disarms and removes the exception handler; call before the DLL unloads.
    void shutdown() noexcept;
};

}  // namespace btd5loader::runtime
