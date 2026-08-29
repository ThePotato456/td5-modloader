// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <string>

namespace btd5loader::runtime {

class BloonActionHook final {
public:
    using Callback = std::function<void(void*)>;

    BloonActionHook() = default;
    ~BloonActionHook();

    BloonActionHook(const BloonActionHook&) = delete;
    BloonActionHook& operator=(const BloonActionHook&) = delete;

    [[nodiscard]] bool install(
        void* primary_spawn,
        void* secondary_spawn,
        void* pop_commit,
        void* leak_commit,
        Callback spawning,
        Callback popping,
        Callback leaking,
        std::string& error);
    void remove() noexcept;
    [[nodiscard]] bool installed() const noexcept;

    static void __stdcall dispatch_leaking(void* bloon) noexcept;

private:
    using SpawnFunction = void(__thiscall*)(void*, void*);
    using PopFunction = void(__thiscall*)(void*, void*, void*, bool);

    static void __fastcall hooked_primary_spawn(
        void* manager,
        void* register_padding,
        void* bloon) noexcept;
    static void __fastcall hooked_secondary_spawn(
        void* manager,
        void* register_padding,
        void* bloon) noexcept;
    static void __fastcall hooked_pop(
        void* manager,
        void* register_padding,
        void* bloon,
        void* child_layers,
        bool forced) noexcept;

    static BloonActionHook* active_;
    static SpawnFunction original_primary_spawn_;
    static SpawnFunction original_secondary_spawn_;
    static PopFunction original_pop_;

    Callback spawning_;
    Callback popping_;
    Callback leaking_;
    void* primary_spawn_{};
    void* secondary_spawn_{};
    void* pop_commit_{};
    void* leak_commit_{};
    std::array<std::byte, 7> leak_original_{};
    bool installed_{};
};

}  // namespace btd5loader::runtime
