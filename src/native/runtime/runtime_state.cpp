#include "runtime_state.hpp"

namespace btd5loader::runtime {

State StateMachine::current() const noexcept {
    return current_.load(std::memory_order_acquire);
}

bool StateMachine::transition_to(const State next) noexcept {
    State current = current_.load(std::memory_order_acquire);
    for (;;) {
        if (!is_valid_transition(current, next)) {
            return false;
        }
        if (current_.compare_exchange_weak(
                current,
                next,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return true;
        }
    }
}

bool StateMachine::is_valid_transition(const State current, const State next) noexcept {
    if (next == State::Failed && current != State::ShuttingDown) {
        return true;
    }
    if (next == State::ShuttingDown && current != State::NotStarted &&
        current != State::ShuttingDown) {
        return true;
    }

    switch (current) {
    case State::NotStarted:
        return next == State::Bootstrap;
    case State::Bootstrap:
        return next == State::CompatibilityCheck;
    case State::CompatibilityCheck:
        return next == State::HooksReady;
    case State::HooksReady:
        return next == State::ModsLoading;
    case State::ModsLoading:
        return next == State::GameReady;
    case State::GameReady:
        return false;
    case State::Failed:
        return next == State::ShuttingDown;
    case State::ShuttingDown:
        return false;
    }
    return false;
}

}  // namespace btd5loader::runtime
