// SPDX-License-Identifier: GPL-3.0-only
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../src/native/runtime/mod_dispatch.hpp"
#include "../../src/native/runtime/object_events.hpp"

using namespace btd5loader::runtime;

namespace {

// A loaded profile of real Lua mods delivered through the production
// coordinator, as the runtime does.
struct Profile final {
    GameObjectRegistry registry;
    GameObjectEvents events{registry};
    std::vector<std::unique_ptr<LuaMod>> mods;
    std::vector<std::string> logs;
    std::vector<std::string> delivered;

    void add(const std::string& script) {
        LuaModOptions options;
        options.mod_id = "objects" + std::to_string(mods.size());
        options.object_registry = &registry;
        // Handler failures (including failed Lua asserts) are logged as errors.
        options.log = [this](const std::string_view level, const std::string_view message) {
            logs.emplace_back(level == "error" ? "error:" + std::string(message)
                                               : std::string(message));
        };
        auto mod = std::make_unique<LuaMod>(options);
        REQUIRE(mod->load_script(script, "objects.lua"));
        mods.push_back(std::move(mod));
    }

    // Delivers once through the production profile traversal and reports
    // cancellation, like the runtime's tower and bloon delivery.
    [[nodiscard]] auto profile(const bool cancellable = false) {
        return [this, cancellable](const std::string_view name, const LuaEventFields& fields) {
            delivered.emplace_back(name);
            return dispatch_profile_event(mods, name, fields, cancellable);
        };
    }

    [[nodiscard]] std::size_t live(const GameObjectKind kind) const {
        return registry.live_count(kind);
    }
};

}  // namespace

TEST_CASE("match wrapper is stable through ending and stale in match.ended", "[objects][events]") {
    Profile profile;
    profile.add(R"(
        local saved
        btd5.events.on('match.starting', function(e)
            saved = e.match; assert(saved:is_valid() and saved:kind() == 'match')
        end)
        for _, name in ipairs({'match.started', 'match.ending'}) do
            btd5.events.on(name, function(e)
                assert(e.match:is_valid() and e.match:id() == saved:id())
            end)
        end
        btd5.events.on('match.ended', function(e)
            assert(not e.match:is_valid() and not saved:is_valid())
            btd5.log('info', 'ended')
        end)
    )");
    int match{};
    int other{};
    profile.events.reset_scene();
    REQUIRE_FALSE(profile.events.match_live());
    profile.events.match_event("match.starting", &match, false, profile.profile());
    REQUIRE(profile.events.match_live());
    // A different screen pointer later in the scene keeps the captured wrapper.
    profile.events.match_event("match.started", &other, false, profile.profile());
    profile.events.match_event("match.ending", &match, false, profile.profile());
    profile.events.begin_round(&other);
    REQUIRE(profile.live(GameObjectKind::Round) == 1);
    profile.events.match_event("match.ended", &match, true, profile.profile());
    REQUIRE_FALSE(profile.events.match_live());
    REQUIRE(profile.registry.live_count() == 0);
    REQUIRE(profile.logs == std::vector<std::string>{"ended"});

    // The next match entry captures a fresh wrapper; rounds carry none until
    // a new receiver begins.
    profile.events.reset_scene();
    profile.events.match_event("match.starting", &match, false, profile.profile());
    REQUIRE(profile.live(GameObjectKind::Match) == 1);
    std::vector<std::size_t> round_fields;
    profile.events.round_event("round.starting", false,
        [&](std::string_view, const LuaEventFields& fields) {
            round_fields.push_back(fields.size());
            return false;
        });
    REQUIRE(round_fields == std::vector<std::size_t>{0});
}

TEST_CASE("match events without a native screen carry no wrapper", "[objects][events]") {
    Profile profile;
    std::vector<std::size_t> sizes;
    const auto record = [&](std::string_view, const LuaEventFields& fields) {
        sizes.push_back(fields.size());
        return false;
    };
    int round{};
    profile.events.begin_round(&round);
    profile.events.match_event("match.starting", nullptr, false, record);
    profile.events.match_event("match.ended", nullptr, true, record);
    REQUIRE(sizes == std::vector<std::size_t>{0, 0});
    REQUIRE_FALSE(profile.events.match_live());
    // match.ended still forgets the round receiver.
    profile.events.round_event("round.ended", true, record);
    REQUIRE(sizes.back() == 0);
}

TEST_CASE("round wrapper stays valid through round.ended and is replaced by the next receiver",
          "[objects][events]") {
    Profile profile;
    profile.add(R"(
        local saved
        btd5.events.on('round.starting', function(e)
            if saved then btd5.log('info', 'previous:' .. tostring(saved:is_valid())) end
            saved = e.round; assert(saved:kind() == 'round')
        end)
        for _, name in ipairs({'round.started', 'round.ending', 'round.ended'}) do
            btd5.events.on(name, function(e)
                assert(e.round:is_valid() and e.round:id() == saved:id())
            end)
        end
    )");
    int first{};
    int second{};
    profile.events.begin_round(&first);
    for (const auto* name : {"round.starting", "round.started", "round.ending"}) {
        profile.events.round_event(name, false, profile.profile());
    }
    profile.events.round_event("round.ended", true, profile.profile());
    REQUIRE(profile.live(GameObjectKind::Round) == 0);

    // A receiver that never reached round.ended is invalidated by its successor.
    profile.events.begin_round(&first);
    profile.events.round_event("round.starting", false, profile.profile());
    profile.events.begin_round(&second);
    REQUIRE(profile.live(GameObjectKind::Round) == 1);
    profile.events.round_event("round.starting", false, profile.profile());
    REQUIRE(profile.logs == std::vector<std::string>{"previous:false", "previous:false"});
}

TEST_CASE("player wrapper is reused within a scene and replaced after a scene reset",
          "[objects][events]") {
    Profile profile;
    int state{};
    const auto first = profile.events.player(&state);
    REQUIRE(first.id != 0);
    REQUIRE(profile.events.player(&state) == first);
    profile.events.reset_scene();
    const auto next = profile.events.player(&state);
    REQUIRE(next != first);
    REQUIRE(profile.registry.resolve(first, GameObjectKind::Player) == nullptr);
    REQUIRE(profile.registry.resolve(next, GameObjectKind::Player) == &state);
}

TEST_CASE("terminal tower and bloon events invalidate their wrapper after every handler",
          "[objects][events]") {
    Profile profile;
    for (int mod = 0; mod < 2; ++mod) {
        profile.add(R"(
            for _, name in ipairs({'tower.placing', 'tower.placed', 'tower.upgrading',
                    'tower.upgraded', 'tower.selling', 'tower.sold'}) do
                btd5.events.on(name, function(e)
                    assert(e.tower:is_valid() and e.tower:kind() == 'tower')
                end)
            end
            for _, name in ipairs({'bloon.spawning', 'bloon.spawned', 'bloon.popping',
                    'bloon.popped', 'bloon.leaking', 'bloon.leaked'}) do
                btd5.events.on(name, function(e)
                    assert(e.bloon:is_valid() and e.bloon:kind() == 'bloon')
                end)
            end
        )");
    }
    int tower{};
    for (const auto* name : {"tower.placing", "tower.placed", "tower.upgrading",
                             "tower.upgraded", "tower.selling"}) {
        REQUIRE(profile.events.object_event(GameObjectKind::Tower, "tower", &tower, name,
                                            false, {}, profile.profile()) == false);
    }
    const auto tower_handle = profile.registry.find(GameObjectKind::Tower, &tower);
    REQUIRE(tower_handle.has_value());
    REQUIRE(profile.events.object_event(GameObjectKind::Tower, "tower", &tower, "tower.sold",
                                        true, {}, profile.profile()) == false);
    REQUIRE(profile.registry.resolve(*tower_handle, GameObjectKind::Tower) == nullptr);

    for (const auto* terminal : {"bloon.popped", "bloon.leaked"}) {
        int bloon{};
        for (const auto* name : {"bloon.spawning", "bloon.spawned", "bloon.popping",
                                 "bloon.leaking"}) {
            REQUIRE(profile.events.object_event(GameObjectKind::Bloon, "bloon", &bloon, name,
                                                false, {}, profile.profile()) == false);
        }
        const auto handle = profile.registry.find(GameObjectKind::Bloon, &bloon);
        REQUIRE(handle.has_value());
        REQUIRE(profile.events.object_event(GameObjectKind::Bloon, "bloon", &bloon, terminal,
                                            true, {}, profile.profile()) == false);
        REQUIRE(profile.registry.resolve(*handle, GameObjectKind::Bloon) == nullptr);
    }
    REQUIRE(profile.logs.empty());
    REQUIRE(profile.delivered.size() == 16);
}

TEST_CASE("cancelled pre-events report cancellation and keep their wrapper live",
          "[objects][events]") {
    Profile profile;
    profile.add(R"(
        for _, name in ipairs({'tower.upgrading', 'tower.selling', 'bloon.leaking'}) do
            btd5.events.on(name, function(e) e.cancelled = true end)
        end
        btd5.events.on('tower.placing', function(e)
            e.cancelled = e.purchase
            btd5.log('info', 'purchase:' .. tostring(e.purchase))
        end)
    )");
    int tower{};
    int bloon{};
    for (const auto* name : {"tower.upgrading", "tower.selling"}) {
        REQUIRE(profile.events.object_event(GameObjectKind::Tower, "tower", &tower, name, false,
                                            {}, profile.profile(true)) == true);
    }
    REQUIRE(profile.events.object_event(GameObjectKind::Bloon, "bloon", &bloon, "bloon.leaking",
                                        false, {}, profile.profile(true)) == true);
    REQUIRE(profile.live(GameObjectKind::Tower) == 1);
    REQUIRE(profile.live(GameObjectKind::Bloon) == 1);

    // Only a purchase is cancellable; the manager-boundary placing is not.
    int bought{};
    REQUIRE(profile.events.object_event(GameObjectKind::Tower, "tower", &bought, "tower.placing",
                                        false, {{"purchase", true}}, profile.profile(true)) == true);
    REQUIRE(profile.events.object_event(GameObjectKind::Tower, "tower", &bought, "tower.placing",
                                        false, {{"purchase", false}}, profile.profile()) == false);
    REQUIRE(profile.logs == std::vector<std::string>{"purchase:true", "purchase:false"});
}

TEST_CASE("a cancelled rejectable spawn is invalidated at once; others stay live",
          "[objects][events]") {
    Profile profile;
    profile.add(R"(
        btd5.events.on('bloon.spawning', function(e)
            e.cancelled = true
            btd5.log('info', 'from_round:' .. tostring(e.from_round))
        end)
    )");
    int round_bloon{};
    int ability_bloon{};
    REQUIRE(profile.events.rejectable_object_event(
                GameObjectKind::Bloon, "bloon", &round_bloon, "bloon.spawning",
                {{"from_round", true}}, profile.profile(true)) == true);
    REQUIRE(profile.live(GameObjectKind::Bloon) == 0);
    // A non-cancellable spawn ignores the handler and keeps its wrapper.
    REQUIRE(profile.events.rejectable_object_event(
                GameObjectKind::Bloon, "bloon", &ability_bloon, "bloon.spawning",
                {{"from_round", false}}, profile.profile()) == false);
    REQUIRE(profile.live(GameObjectKind::Bloon) == 1);
    REQUIRE(profile.logs == std::vector<std::string>{"from_round:true", "from_round:false"});

    // A throwing delivery cannot cancel, so the wrapper stays live.
    int other{};
    const auto throwing = [](std::string_view, const LuaEventFields&) -> bool {
        throw std::runtime_error("delivery failed");
    };
    REQUIRE_THROWS(profile.events.rejectable_object_event(
        GameObjectKind::Bloon, "bloon", &other, "bloon.spawning", {}, throwing));
    REQUIRE(profile.live(GameObjectKind::Bloon) == 2);
}

TEST_CASE("terminal wrappers are invalidated even when delivery throws", "[objects][events]") {
    Profile profile;
    int tower{};
    const auto throwing = [](std::string_view, const LuaEventFields&) -> bool {
        throw std::runtime_error("delivery failed");
    };
    REQUIRE_THROWS(profile.events.object_event(GameObjectKind::Tower, "tower", &tower,
                                               "tower.sold", true, {}, throwing));
    REQUIRE(profile.live(GameObjectKind::Tower) == 0);
    REQUIRE_THROWS(profile.events.object_event(GameObjectKind::Tower, "tower", &tower,
                                               "tower.selling", false, {}, throwing));
    REQUIRE(profile.live(GameObjectKind::Tower) == 1);
}

TEST_CASE("a delivered purchase suppresses only its own manager-boundary placing",
          "[objects][events]") {
    Profile profile;
    int bought{};
    int granted{};
    REQUIRE_FALSE(profile.events.take_purchase(nullptr));
    profile.events.purchase_delivered(&bought, false);
    REQUIRE_FALSE(profile.events.take_purchase(&granted));
    REQUIRE(profile.events.take_purchase(&bought));
    REQUIRE_FALSE(profile.events.take_purchase(&bought));

    // A cancelled purchase never reaches the ownership boundary.
    profile.events.purchase_delivered(&bought, true);
    REQUIRE_FALSE(profile.events.take_purchase(&bought));

    // Match entry forgets a purchase still pending from the previous scene.
    profile.events.purchase_delivered(&bought, false);
    profile.events.reset_scene();
    REQUIRE_FALSE(profile.events.take_purchase(&bought));
}

TEST_CASE("attacks and projectiles are tracked only inside a live match", "[objects][events]") {
    Profile profile;
    profile.add(R"(
        local saved = {}
        for _, kind in ipairs({'attack', 'projectile'}) do
            btd5.events.on(kind .. '.created', function(e)
                saved[kind] = e[kind]; assert(e[kind]:is_valid() and e[kind]:kind() == kind)
            end)
            btd5.events.on(kind .. '.destroyed', function(e)
                assert(e[kind]:is_valid() and e[kind]:id() == saved[kind]:id())
                btd5.log('info', kind .. ':destroyed')
            end)
        end
    )");
    using Tracking = GameObjectEvents::Tracking;
    int prototype{};
    REQUIRE(profile.events.object_created(GameObjectKind::Attack, &prototype, profile.profile()) ==
            Tracking::Ignored);
    REQUIRE(profile.delivered.empty());
    REQUIRE_FALSE(profile.events.object_destroying(GameObjectKind::Attack, &prototype,
                                                   profile.profile()));

    int match{};
    profile.events.match_event("match.starting", &match, false, profile.profile());
    int attack{};
    int projectile{};
    REQUIRE(profile.events.object_created(GameObjectKind::Attack, &attack, profile.profile()) ==
            Tracking::Tracked);
    REQUIRE(profile.events.object_created(GameObjectKind::Attack, &attack, profile.profile()) ==
            Tracking::Ignored);
    REQUIRE(profile.events.object_created(GameObjectKind::Projectile, &projectile,
                                          profile.profile()) == Tracking::Tracked);
    REQUIRE(profile.live(GameObjectKind::Attack) == 1);
    REQUIRE(profile.live(GameObjectKind::Projectile) == 1);

    REQUIRE(profile.events.object_destroying(GameObjectKind::Projectile, &projectile,
                                             profile.profile()));
    REQUIRE(profile.events.object_destroying(GameObjectKind::Attack, &attack, profile.profile()));
    REQUIRE(profile.live(GameObjectKind::Attack) == 0);
    REQUIRE(profile.live(GameObjectKind::Projectile) == 0);
    REQUIRE(profile.logs == std::vector<std::string>{"projectile:destroyed", "attack:destroyed"});
    REQUIRE(profile.delivered == std::vector<std::string>{"match.starting", "attack.created",
                                     "projectile.created", "projectile.destroyed",
                                     "attack.destroyed"});
}

TEST_CASE("tower and bloon destructors invalidate without delivering an event",
          "[objects][events]") {
    Profile profile;
    int tower{};
    int bloon{};
    const auto tower_handle = profile.registry.add(GameObjectKind::Tower, &tower);
    const auto bloon_handle = profile.registry.add(GameObjectKind::Bloon, &bloon);
    REQUIRE(profile.events.object_destroying(GameObjectKind::Tower, &tower, profile.profile()));
    REQUIRE(profile.events.object_destroying(GameObjectKind::Bloon, &bloon, profile.profile()));
    REQUIRE(profile.delivered.empty());
    REQUIRE(profile.registry.resolve(tower_handle, GameObjectKind::Tower) == nullptr);
    REQUIRE(profile.registry.resolve(bloon_handle, GameObjectKind::Bloon) == nullptr);
    // A destructor after the terminal event finds nothing left to invalidate.
    REQUIRE_FALSE(profile.events.object_destroying(GameObjectKind::Tower, &tower,
                                                   profile.profile()));
}

TEST_CASE("destroyed wrappers are invalidated even when delivery throws", "[objects][events]") {
    Profile profile;
    int match{};
    int attack{};
    profile.events.match_event("match.starting", &match, false, profile.profile());
    REQUIRE(profile.events.object_created(GameObjectKind::Attack, &attack, profile.profile()) ==
            GameObjectEvents::Tracking::Tracked);
    REQUIRE_THROWS(profile.events.object_destroying(GameObjectKind::Attack, &attack,
        [](std::string_view, const LuaEventFields&) -> bool {
            throw std::runtime_error("delivery failed");
        }));
    REQUIRE(profile.live(GameObjectKind::Attack) == 0);
}
