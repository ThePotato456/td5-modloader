// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "game_object.hpp"
#include "lua_mod.hpp"

namespace btd5loader::runtime {

// Ties live gameplay wrappers to their verified native boundaries: which handle
// each event carries and when that handle becomes stale. Delivery callbacks have
// the signature bool(std::string_view name, const LuaEventFields&) and return
// true when a mod cancelled the event; the caller owns state checks, the
// dispatch gate, and logging.
class GameObjectEvents final {
public:
    enum class Tracking { Ignored, Unavailable, Tracked };

    explicit GameObjectEvents(GameObjectRegistry& registry) noexcept : registry_(registry) {}

    // Match entry and runtime shutdown: every handle from the previous scene
    // becomes stale.
    void reset_scene() noexcept {
        registry_.begin_scene();
        match_.reset();
        round_.reset();
        purchased_tower_ = nullptr;
    }

    [[nodiscard]] bool match_live() const noexcept { return match_.has_value(); }

    // The match wrapper is captured once per scene. On the ended boundary the
    // scene is reset before delivery, so match.ended carries a stale wrapper.
    template<class Deliver>
    void match_event(const std::string_view name, void* const match, const bool ended,
                     Deliver&& deliver) {
        if (!match_.has_value() && match != nullptr) {
            const auto handle = registry_.find_or_add(GameObjectKind::Match, match);
            if (handle.id != 0) match_ = handle;
        }
        if (!match_.has_value()) {
            (void)deliver(name, LuaEventFields{});
        } else {
            const auto handle = *match_;
            if (ended) registry_.begin_scene();
            (void)deliver(name, LuaEventFields{{"match", handle}});
        }
        if (ended) {
            match_.reset();
            round_.reset();
        }
    }

    // A new round receiver replaces, and invalidates, any earlier one.
    void begin_round(void* const round) {
        if (round_.has_value()) (void)registry_.invalidate(*round_);
        const auto handle = registry_.add(GameObjectKind::Round, round);
        round_ = handle.id != 0 ? std::optional<GameObjectHandle>{handle} : std::nullopt;
    }

    // The round wrapper stays valid through round.ended and is invalidated
    // after that delivery returns.
    template<class Deliver>
    void round_event(const std::string_view name, const bool ended, Deliver&& deliver) {
        if (!round_.has_value()) {
            (void)deliver(name, LuaEventFields{});
            return;
        }
        const auto handle = *round_;
        (void)deliver(name, LuaEventFields{{"round", handle}});
        if (ended) {
            (void)registry_.invalidate(handle);
            round_.reset();
        }
    }

    // The lives-state object keeps one wrapper within the current scene.
    [[nodiscard]] GameObjectHandle player(void* const state) {
        return registry_.find_or_add(GameObjectKind::Player, state);
    }

    // Delivers a tower or bloon event with its wrapper under `field`, followed by
    // `extra` fields. A terminal event invalidates the wrapper after delivery,
    // even when delivery throws. Returns nullopt when no handle is available,
    // otherwise whether a mod cancelled the event.
    template<class Deliver>
    [[nodiscard]] std::optional<bool> object_event(
        const GameObjectKind kind, const std::string_view field, void* const object,
        const std::string_view name, const bool invalidate_after, LuaEventFields extra,
        Deliver&& deliver) {
        const auto handle = registry_.find_or_add(kind, object);
        if (handle.id == 0) return std::nullopt;
        const InvalidateOnExit invalidate{registry_, handle, invalidate_after};
        LuaEventFields fields{{std::string(field), handle}};
        for (auto& entry : extra) fields.push_back(std::move(entry));
        return static_cast<bool>(deliver(name, fields));
    }

    // Like object_event, but a cancelled delivery invalidates the wrapper as soon
    // as it returns: the object never joins the game and is destroyed later.
    template<class Deliver>
    [[nodiscard]] std::optional<bool> rejectable_object_event(
        const GameObjectKind kind, const std::string_view field, void* const object,
        const std::string_view name, LuaEventFields extra, Deliver&& deliver) {
        const auto handle = registry_.find_or_add(kind, object);
        if (handle.id == 0) return std::nullopt;
        InvalidateOnExit invalidate{registry_, handle, false};
        LuaEventFields fields{{std::string(field), handle}};
        for (auto& entry : extra) fields.push_back(std::move(entry));
        const bool cancelled = static_cast<bool>(deliver(name, fields));
        invalidate.enabled = cancelled;
        return cancelled;
    }

    // A purchase delivers tower.placing before manager ownership; the
    // ownership boundary must not deliver it a second time. A cancelled
    // purchase never reaches that boundary.
    void purchase_delivered(void* const tower, const bool cancelled) noexcept {
        purchased_tower_ = cancelled ? nullptr : tower;
    }

    [[nodiscard]] bool take_purchase(void* const tower) noexcept {
        if (tower == nullptr || tower != purchased_tower_) return false;
        purchased_tower_ = nullptr;
        return true;
    }

    // Attacks and projectiles are tracked only when created inside a live
    // match scene; factory prototypes built during asset loading never are.
    template<class Deliver>
    [[nodiscard]] Tracking object_created(const GameObjectKind kind, void* const object,
                                          Deliver&& deliver) {
        if (!match_.has_value() || registry_.find(kind, object).has_value()) {
            return Tracking::Ignored;
        }
        const auto handle = registry_.add(kind, object);
        if (handle.id == 0) return Tracking::Unavailable;
        (void)deliver(lifetime_event_name(kind, true), lifetime_fields(kind, handle));
        return Tracking::Tracked;
    }

    // A native destructor invalidates any tracked wrapper. Towers and bloons
    // already deliver their own terminal events, so only attacks and
    // projectiles deliver *.destroyed first. Returns false when untracked.
    template<class Deliver>
    [[nodiscard]] bool object_destroying(const GameObjectKind kind, void* const object,
                                         Deliver&& deliver) {
        const auto handle = registry_.find(kind, object);
        if (!handle.has_value()) return false;
        const InvalidateOnExit invalidate{registry_, *handle, true};
        if (kind == GameObjectKind::Attack || kind == GameObjectKind::Projectile) {
            (void)deliver(lifetime_event_name(kind, false), lifetime_fields(kind, *handle));
        }
        return true;
    }

    [[nodiscard]] static std::string_view lifetime_event_name(
        const GameObjectKind kind, const bool created) noexcept {
        if (kind == GameObjectKind::Attack) {
            return created ? "attack.created" : "attack.destroyed";
        }
        return created ? "projectile.created" : "projectile.destroyed";
    }

private:
    struct InvalidateOnExit final {
        GameObjectRegistry& registry;
        GameObjectHandle handle;
        bool enabled;
        ~InvalidateOnExit() {
            if (enabled) (void)registry.invalidate(handle);
        }
    };

    [[nodiscard]] static LuaEventFields lifetime_fields(
        const GameObjectKind kind, const GameObjectHandle& handle) {
        return {{kind == GameObjectKind::Attack ? "attack" : "projectile", handle}};
    }

    GameObjectRegistry& registry_;
    std::optional<GameObjectHandle> match_;
    std::optional<GameObjectHandle> round_;
    void* purchased_tower_{};
};

}  // namespace btd5loader::runtime
