#include <btd5loader/version.hpp>
#include <btd5loader/runtime_api.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../../src/native/runtime/runtime_state.hpp"

TEST_CASE("product metadata is available", "[foundation]") {
    REQUIRE(btd5loader::kProductName == "BTD5 Mod Loader");
    REQUIRE_FALSE(btd5loader::kVersion.empty());
}

TEST_CASE("native targets are built for the 32-bit game", "[foundation]") {
    STATIC_REQUIRE(sizeof(void*) == 4);
}

TEST_CASE("runtime lifecycle accepts only forward transitions", "[runtime]") {
    using btd5loader::runtime::State;
    btd5loader::runtime::StateMachine state;

    REQUIRE(state.current() == State::NotStarted);
    REQUIRE(state.transition_to(State::Bootstrap));
    REQUIRE_FALSE(state.transition_to(State::GameReady));
    REQUIRE(state.transition_to(State::CompatibilityCheck));
    REQUIRE(state.transition_to(State::HooksReady));
    REQUIRE(state.transition_to(State::ModsLoading));
    REQUIRE(state.transition_to(State::GameReady));
    REQUIRE(state.transition_to(State::ShuttingDown));
    REQUIRE_FALSE(state.transition_to(State::Failed));
}

TEST_CASE("runtime lifecycle can fail closed", "[runtime]") {
    using btd5loader::runtime::State;
    btd5loader::runtime::StateMachine state;

    REQUIRE(state.transition_to(State::Failed));
    REQUIRE(state.current() == State::Failed);
    REQUIRE(state.transition_to(State::ShuttingDown));
}
