#pragma once
// FixtureEditRequests: the hand-over between the Fixture Library panel ("Edit" button) and the
// Fixture Editor panel. It is the one place where two panels talk to each other, so it stays
// tiny: the library asks for a fixture id, the editor picks the request up in its next draw().
//
// The editor registers its window's `open` flag, so a request also brings back an editor window
// that was closed. After "Apply" the editor bumps appliedCount(); panels that show library
// contents (the library list) compare it with the value they saw to refresh themselves.
// Main thread only.

#include <cstdint>
#include <optional>
#include <string>

namespace dmxviz::ui {

class FixtureEditRequests {
public:
    // Ask the Fixture Editor to edit a copy of the library fixture `fixtureId`.
    static void request(std::string fixtureId);
    // The pending request, once.
    static std::optional<std::string> take();

    // The editor window's open flag (Panel::open); request() sets it. Detach when the panel dies.
    static void attachWindowFlag(bool* open);
    static void detachWindowFlag(bool* open);

    // Incremented every time the editor wrote a fixture into the library.
    static std::uint64_t appliedCount();
    static void notifyApplied();
};

}  // namespace dmxviz::ui
