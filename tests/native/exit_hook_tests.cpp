// SPDX-License-Identifier: GPL-3.0-only
#include <Windows.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <future>
#include <stdexcept>

#include "../../src/native/runtime/exit_hook.hpp"
#include "../../src/native/runtime/mod_lifecycle.hpp"

namespace {
bool __cdecl predicate_true() { return true; }
bool __cdecl predicate_false() { return false; }
}

TEST_CASE("production dispatch gate drains active work and refuses later work", "[shutdown]") {
    using namespace btd5loader::runtime;
    ModDispatchGate gate;
    std::promise<void> stop_requested;
    auto stopped = stop_requested.get_future();
    std::future<bool> cleanup;
    {
        auto active = gate.enter();
        REQUIRE(active);
        cleanup = std::async(std::launch::async, [&]() {
            const bool first = gate.request_stop();
            stop_requested.set_value();
            auto drained = gate.exclusive();
            return first;
        });
        stopped.wait();
        CHECK(gate.stopping());
        CHECK(cleanup.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout);
    }
    REQUIRE(cleanup.get());
    REQUIRE_FALSE(gate.request_stop());
    REQUIRE_FALSE(gate.enter());
}

TEST_CASE("production mod cleanup runs once in reverse order despite a failed callback", "[shutdown]") {
    using namespace btd5loader::runtime;
    std::vector<std::string> calls;
    std::vector<std::unique_ptr<LuaMod>> mods;
    shutdown_mods(mods); // Partial initialization with no eligible mods.
    for (const std::string id : {"first", "failing", "last"}) {
        LuaModOptions options;
        options.mod_id = id;
        options.log = [&](auto, auto message) { calls.emplace_back(message); };
        auto mod = std::make_unique<LuaMod>(options);
        REQUIRE(mod->load_script("function on_shutdown() btd5.log('info', '" + id + "') " +
            (id == "failing" ? "error('shutdown probe') " : "") + "end", "shutdown.lua"));
        mods.push_back(std::move(mod));
    }
    shutdown_mods(mods);
    REQUIRE(mods.empty());
    const auto last = std::find(calls.begin(), calls.end(), "last");
    const auto failed = std::find(calls.begin(), calls.end(), "failing");
    const auto first = std::find(calls.begin(), calls.end(), "first");
    REQUIRE(last != calls.end());
    REQUIRE(failed != calls.end());
    REQUIRE(first != calls.end());
    REQUIRE(last < failed);
    REQUIRE(failed < first);
    const auto count = calls.size();
    shutdown_mods(mods);
    REQUIRE(calls.size() == count);
}

TEST_CASE("normal exit boundary preserves the CRT call after self removal", "[hooks][shutdown]") {
    using btd5loader::runtime::ExitHook;
    for (auto predicate : {&predicate_true, &predicate_false}) {
        auto* allocation = static_cast<std::byte*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE,
                                                        PAGE_EXECUTE_READWRITE));
        REQUIRE(allocation != nullptr);
        auto* code = allocation + 3;
        code[0] = std::byte{0xE8};
        const auto displacement = static_cast<std::int32_t>(
            reinterpret_cast<std::uintptr_t>(predicate) - (reinterpret_cast<std::uintptr_t>(code) + 5));
        std::memcpy(code + 1, &displacement, sizeof(displacement));
        code[5] = std::byte{0xC3};
        std::array<std::byte, 6> original{};
        std::memcpy(original.data(), code, original.size());
        int calls = 0;
        ExitHook hook;
        std::string error;
        REQUIRE_FALSE(hook.install(code, predicate == &predicate_true ? &predicate_false : &predicate_true,
                                   []() {}, error));
        REQUIRE(hook.install(code, predicate, [&]() { ++calls; hook.remove(); }, error));
        const auto run = reinterpret_cast<ExitHook::Predicate>(code);
        REQUIRE(run() == predicate());
        REQUIRE(calls == 1);
        REQUIRE(std::memcmp(code, original.data(), original.size()) == 0);
        REQUIRE(run() == predicate());
        REQUIRE(calls == 1);
        hook.remove();
        REQUIRE(hook.install(code, predicate, []() { throw std::runtime_error("cleanup failure"); }, error));
        REQUIRE(run() == predicate());
        hook.remove();
        REQUIRE(std::memcmp(code, original.data(), original.size()) == 0);
        VirtualFree(allocation, 0, MEM_RELEASE);
    }
}
