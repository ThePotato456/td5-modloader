#pragma once

#include <atomic>

#include <btd5loader/runtime_api.hpp>

namespace btd5loader::runtime {

class StateMachine final {
public:
    [[nodiscard]] State current() const noexcept;
    [[nodiscard]] bool transition_to(State next) noexcept;

private:
    static bool is_valid_transition(State current, State next) noexcept;

    std::atomic<State> current_{State::NotStarted};
};

}  // namespace btd5loader::runtime

