// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include "game_object.hpp"

namespace btd5loader::runtime {

// Observes every native construction and destruction of attack (`CWeapon`) and
// projectile (`CProjectile`) base objects, plus destruction of tower
// (`CBaseTower`) and bloon (`CBloon`) objects. Constructors report after the
// original returns, with the native return address of the constructor call;
// destructors report before the original begins.
class ObjectLifetimeHook final {
public:
    using CreatedCallback = std::function<void(GameObjectKind, void*, const void* caller)>;
    using Callback = std::function<void(GameObjectKind, void*)>;

    struct Targets final {
        void* attack_constructor{};
        void* attack_destructor{};
        std::array<void*, 3> projectile_constructors{};
        void* projectile_destructor{};
        void* tower_destructor{};
        void* bloon_destructor{};
    };

    ObjectLifetimeHook() = default;
    ~ObjectLifetimeHook();

    ObjectLifetimeHook(const ObjectLifetimeHook&) = delete;
    ObjectLifetimeHook& operator=(const ObjectLifetimeHook&) = delete;

    [[nodiscard]] bool install(
        const Targets& targets,
        CreatedCallback created,
        Callback destroying,
        std::string& error);
    void remove() noexcept;
    [[nodiscard]] bool installed() const noexcept;

private:
    using Word = std::uintptr_t;
    using AttackConstructor = void*(__thiscall*)(void*, Word, Word);
    using ProjectileConstructor4 = void*(__thiscall*)(void*, Word, Word, Word, Word);
    using ProjectileConstructor6 =
        void*(__thiscall*)(void*, Word, Word, Word, Word, Word, Word);
    using Destructor = void(__thiscall*)(void*);

    // Constructors are intentionally not noexcept: a native allocation failure
    // must unwind through the detour exactly as it would without the loader.
    static void* __fastcall hooked_attack_constructor(
        void* object, void* register_padding, Word first, Word second);
    static void __fastcall hooked_attack_destructor(
        void* object, void* register_padding) noexcept;
    static void* __fastcall hooked_projectile_constructor_primary(
        void* object, void* register_padding, Word a, Word b, Word c, Word d);
    static void* __fastcall hooked_projectile_constructor_secondary(
        void* object, void* register_padding, Word a, Word b, Word c, Word d);
    static void* __fastcall hooked_projectile_constructor_tertiary(
        void* object,
        void* register_padding,
        Word a,
        Word b,
        Word c,
        Word d,
        Word e,
        Word f);
    static void __fastcall hooked_projectile_destructor(
        void* object, void* register_padding) noexcept;
    static void __fastcall hooked_tower_destructor(
        void* object, void* register_padding) noexcept;
    static void __fastcall hooked_bloon_destructor(
        void* object, void* register_padding) noexcept;

    static void report_created(GameObjectKind kind, void* object, const void* caller) noexcept;
    static void report_destroying(GameObjectKind kind, void* object) noexcept;

    static ObjectLifetimeHook* active_;
    static AttackConstructor original_attack_constructor_;
    static Destructor original_attack_destructor_;
    static ProjectileConstructor4 original_projectile_constructor_primary_;
    static ProjectileConstructor4 original_projectile_constructor_secondary_;
    static ProjectileConstructor6 original_projectile_constructor_tertiary_;
    static Destructor original_projectile_destructor_;
    static Destructor original_tower_destructor_;
    static Destructor original_bloon_destructor_;

    CreatedCallback created_;
    Callback destroying_;
    std::array<void*, 8> targets_{};
    std::size_t created_hooks_{};
    bool installed_{};
};

}  // namespace btd5loader::runtime
