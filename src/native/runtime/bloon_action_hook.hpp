// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <string>

namespace btd5loader::runtime {

class BloonActionHook final {
public:
    using CancellableCallback = std::function<bool(void*)>;
    // Receives the bloon and whether this spawn may be cancelled; returns
    // whether a handler cancelled it.
    using SpawnCallback = std::function<bool(void*, bool)>;

    // Native pieces a cancelled round-emitter spawn needs. Only a primary spawn
    // returning to emitter_return is cancellable; it is then removed the way the
    // manager removes a bloon, without ever joining the manager's list.
    struct SpawnRejection final {
        void* emitter_return{};
        void* queue_push{};     // std::vector<CBloon*>::push_back (__thiscall vector, const value*)
        void* strip_effects{};  // CBloon status-effect removal (__thiscall bloon, char expired_only)
    };

    // Decodes the rejection helpers from their verified call sites: the
    // push_back that primary spawn uses for the manager list and the effect
    // removal that the pop routine calls on the parent.
    [[nodiscard]] static bool resolve_rejection(
        const void* primary_spawn,
        const void* pop_routine,
        void* emitter_return,
        SpawnRejection& rejection,
        std::string& error) noexcept;

    BloonActionHook() = default;
    ~BloonActionHook();

    BloonActionHook(const BloonActionHook&) = delete;
    BloonActionHook& operator=(const BloonActionHook&) = delete;

    // lethal_hit is the damage function's health commit and pop-flag store
    // (17 bytes); popping runs only on the lethal branch, before the flag is set.
    [[nodiscard]] bool install(
        void* primary_spawn,
        void* secondary_spawn,
        const SpawnRejection& rejection,
        void* lethal_hit,
        void* leak_commit,
        SpawnCallback spawning,
        CancellableCallback popping,
        CancellableCallback leaking,
        std::string& error);
    void remove() noexcept;
    [[nodiscard]] bool installed() const noexcept;

    [[nodiscard]] static bool __stdcall dispatch_leaking(void* bloon) noexcept;
    [[nodiscard]] static bool __stdcall dispatch_popping(void* bloon) noexcept;
    [[nodiscard]] static bool __stdcall dispatch_spawning(void* bloon, bool cancellable) noexcept;

private:
    using SpawnFunction = void(__thiscall*)(void*, void*);
    using QueuePushFunction = void(__thiscall*)(void*, void* const*);
    using StripEffectsFunction = void(__thiscall*)(void*, char);

    // Removes a cancelled bloon before it joins the manager: the native removal
    // flags, effect removal, and the manager's deferred-deletion queue.
    // Returns false (and leaves the bloon untouched) if the queue looks invalid.
    [[nodiscard]] bool reject_spawn(void* manager, void* bloon) const noexcept;

    static void __fastcall hooked_primary_spawn(
        void* manager,
        void* register_padding,
        void* bloon) noexcept;
    static void __fastcall hooked_secondary_spawn(
        void* manager,
        void* register_padding,
        void* bloon) noexcept;

    static BloonActionHook* active_;
    static SpawnFunction original_primary_spawn_;
    static SpawnFunction original_secondary_spawn_;

    SpawnCallback spawning_;
    SpawnRejection rejection_;
    CancellableCallback popping_;
    CancellableCallback leaking_;
    void* primary_spawn_{};
    void* secondary_spawn_{};
    void* lethal_hit_{};
    void* leak_commit_{};
    std::array<std::byte, 7> leak_original_{};
    std::array<std::byte, 17> lethal_original_{};
    bool installed_{};
};

}  // namespace btd5loader::runtime
