#include "ui/FixtureEditRequests.h"

#include <utility>

namespace dmxviz::ui {

namespace {

// The UI runs on one thread, so plain state is enough.
struct State {
    std::optional<std::string> pending;
    bool* windowFlag = nullptr;
    std::uint64_t applied = 0;
};

State& state() {
    static State instance;
    return instance;
}

}  // namespace

void FixtureEditRequests::request(std::string fixtureId) {
    State& s = state();
    s.pending = std::move(fixtureId);
    if (s.windowFlag != nullptr) *s.windowFlag = true;
}

std::optional<std::string> FixtureEditRequests::take() {
    return std::exchange(state().pending, std::nullopt);
}

void FixtureEditRequests::attachWindowFlag(bool* open) {
    state().windowFlag = open;
}

void FixtureEditRequests::detachWindowFlag(bool* open) {
    State& s = state();
    if (s.windowFlag == open) s.windowFlag = nullptr;
}

std::uint64_t FixtureEditRequests::appliedCount() {
    return state().applied;
}

void FixtureEditRequests::notifyApplied() {
    ++state().applied;
}

}  // namespace dmxviz::ui
