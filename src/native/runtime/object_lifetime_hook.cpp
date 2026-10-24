// SPDX-License-Identifier: GPL-3.0-only
#include "object_lifetime_hook.hpp"

#include <MinHook.h>
#include <intrin.h>

#include <utility>

namespace btd5loader::runtime {

ObjectLifetimeHook* ObjectLifetimeHook::active_{};
ObjectLifetimeHook::AttackConstructor ObjectLifetimeHook::original_attack_constructor_{};
ObjectLifetimeHook::Destructor ObjectLifetimeHook::original_attack_destructor_{};
ObjectLifetimeHook::ProjectileConstructor4
    ObjectLifetimeHook::original_projectile_constructor_primary_{};
ObjectLifetimeHook::ProjectileConstructor4
    ObjectLifetimeHook::original_projectile_constructor_secondary_{};
ObjectLifetimeHook::ProjectileConstructor6
    ObjectLifetimeHook::original_projectile_constructor_tertiary_{};
ObjectLifetimeHook::Destructor ObjectLifetimeHook::original_projectile_destructor_{};
ObjectLifetimeHook::Destructor ObjectLifetimeHook::original_tower_destructor_{};
ObjectLifetimeHook::Destructor ObjectLifetimeHook::original_bloon_destructor_{};

ObjectLifetimeHook::~ObjectLifetimeHook() {
    remove();
}

bool ObjectLifetimeHook::install(
    const Targets& targets,
    CreatedCallback created,
    Callback destroying,
    std::string& error) {
    if (installed_ || active_ != nullptr) {
        error = "object lifetime hook is already installed";
        return false;
    }
    targets_ = {
        targets.attack_constructor,
        targets.attack_destructor,
        targets.projectile_constructors[0],
        targets.projectile_constructors[1],
        targets.projectile_constructors[2],
        targets.projectile_destructor,
        targets.tower_destructor,
        targets.bloon_destructor};
    for (void* const target : targets_) {
        if (target == nullptr) {
            targets_ = {};
            error = "object lifetime hook target is missing";
            return false;
        }
    }
    if (!created || !destroying) {
        targets_ = {};
        error = "object lifetime hook callback is missing";
        return false;
    }
    const MH_STATUS initialize_status = MH_Initialize();
    if (initialize_status != MH_OK && initialize_status != MH_ERROR_ALREADY_INITIALIZED) {
        targets_ = {};
        error = "MinHook initialization failed";
        return false;
    }

    const std::array<std::pair<void*, void**>, 8> originals{{
        {reinterpret_cast<void*>(&hooked_attack_constructor),
         reinterpret_cast<void**>(&original_attack_constructor_)},
        {reinterpret_cast<void*>(&hooked_attack_destructor),
         reinterpret_cast<void**>(&original_attack_destructor_)},
        {reinterpret_cast<void*>(&hooked_projectile_constructor_primary),
         reinterpret_cast<void**>(&original_projectile_constructor_primary_)},
        {reinterpret_cast<void*>(&hooked_projectile_constructor_secondary),
         reinterpret_cast<void**>(&original_projectile_constructor_secondary_)},
        {reinterpret_cast<void*>(&hooked_projectile_constructor_tertiary),
         reinterpret_cast<void**>(&original_projectile_constructor_tertiary_)},
        {reinterpret_cast<void*>(&hooked_projectile_destructor),
         reinterpret_cast<void**>(&original_projectile_destructor_)},
        {reinterpret_cast<void*>(&hooked_tower_destructor),
         reinterpret_cast<void**>(&original_tower_destructor_)},
        {reinterpret_cast<void*>(&hooked_bloon_destructor),
         reinterpret_cast<void**>(&original_bloon_destructor_)},
    }};
    created_hooks_ = 0;
    for (std::size_t index = 0; index < targets_.size(); ++index) {
        if (MH_CreateHook(targets_[index], originals[index].first, originals[index].second) !=
            MH_OK) {
            error = "object lifetime hook creation failed";
            remove();
            targets_ = {};
            return false;
        }
        ++created_hooks_;
    }

    created_ = std::move(created);
    destroying_ = std::move(destroying);
    active_ = this;
    // Enable destructors first so no constructed object can outlive its
    // invalidation boundary while the remaining hooks are being enabled.
    constexpr std::array<std::size_t, 8> enable_order{1, 5, 6, 7, 0, 2, 3, 4};
    for (const std::size_t index : enable_order) {
        if (MH_EnableHook(targets_[index]) != MH_OK) {
            error = "object lifetime hook enable failed";
            installed_ = true;
            remove();
            return false;
        }
    }
    installed_ = true;
    return true;
}

void ObjectLifetimeHook::remove() noexcept {
    // Disable constructors before destructors, the reverse of installation.
    constexpr std::array<std::size_t, 8> disable_order{4, 3, 2, 0, 7, 6, 5, 1};
    if (installed_) {
        for (const std::size_t index : disable_order) {
            (void)MH_DisableHook(targets_[index]);
        }
    }
    if (active_ == this) {
        active_ = nullptr;
    }
    for (std::size_t index = 0; index < created_hooks_; ++index) {
        (void)MH_RemoveHook(targets_[index]);
    }
    created_hooks_ = 0;
    original_attack_constructor_ = nullptr;
    original_attack_destructor_ = nullptr;
    original_projectile_constructor_primary_ = nullptr;
    original_projectile_constructor_secondary_ = nullptr;
    original_projectile_constructor_tertiary_ = nullptr;
    original_projectile_destructor_ = nullptr;
    original_tower_destructor_ = nullptr;
    original_bloon_destructor_ = nullptr;
    created_ = {};
    destroying_ = {};
    targets_ = {};
    installed_ = false;
}

bool ObjectLifetimeHook::installed() const noexcept {
    return installed_;
}

void ObjectLifetimeHook::report_created(
    const GameObjectKind kind,
    void* const object,
    const void* const caller) noexcept {
    ObjectLifetimeHook* const active = active_;
    if (active == nullptr || object == nullptr || !active->created_) {
        return;
    }
    try {
        active->created_(kind, object, caller);
    } catch (...) {
        // Keep the native construction boundary exception-safe.
    }
}

void ObjectLifetimeHook::report_destroying(
    const GameObjectKind kind,
    void* const object) noexcept {
    ObjectLifetimeHook* const active = active_;
    if (active == nullptr || object == nullptr || !active->destroying_) {
        return;
    }
    try {
        active->destroying_(kind, object);
    } catch (...) {
        // Keep the native destruction boundary exception-safe.
    }
}

void* __fastcall ObjectLifetimeHook::hooked_attack_constructor(
    void* const object,
    void*,
    const Word first,
    const Word second) {
    void* const result = original_attack_constructor_(object, first, second);
    report_created(GameObjectKind::Attack, object, _ReturnAddress());
    return result;
}

void __fastcall ObjectLifetimeHook::hooked_attack_destructor(void* const object, void*) noexcept {
    report_destroying(GameObjectKind::Attack, object);
    original_attack_destructor_(object);
}

void* __fastcall ObjectLifetimeHook::hooked_projectile_constructor_primary(
    void* const object,
    void*,
    const Word a,
    const Word b,
    const Word c,
    const Word d) {
    void* const result = original_projectile_constructor_primary_(object, a, b, c, d);
    report_created(GameObjectKind::Projectile, object, _ReturnAddress());
    return result;
}

void* __fastcall ObjectLifetimeHook::hooked_projectile_constructor_secondary(
    void* const object,
    void*,
    const Word a,
    const Word b,
    const Word c,
    const Word d) {
    void* const result = original_projectile_constructor_secondary_(object, a, b, c, d);
    report_created(GameObjectKind::Projectile, object, _ReturnAddress());
    return result;
}

void* __fastcall ObjectLifetimeHook::hooked_projectile_constructor_tertiary(
    void* const object,
    void*,
    const Word a,
    const Word b,
    const Word c,
    const Word d,
    const Word e,
    const Word f) {
    void* const result =
        original_projectile_constructor_tertiary_(object, a, b, c, d, e, f);
    report_created(GameObjectKind::Projectile, object, _ReturnAddress());
    return result;
}

void __fastcall ObjectLifetimeHook::hooked_projectile_destructor(
    void* const object,
    void*) noexcept {
    report_destroying(GameObjectKind::Projectile, object);
    original_projectile_destructor_(object);
}

void __fastcall ObjectLifetimeHook::hooked_tower_destructor(void* const object, void*) noexcept {
    report_destroying(GameObjectKind::Tower, object);
    original_tower_destructor_(object);
}

void __fastcall ObjectLifetimeHook::hooked_bloon_destructor(void* const object, void*) noexcept {
    report_destroying(GameObjectKind::Bloon, object);
    original_bloon_destructor_(object);
}

}  // namespace btd5loader::runtime
