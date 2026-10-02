#pragma once
// Small helpers for DMX tests that involve real sockets and threads.

#include "dmx/DmxSnapshot.h"
#include "dmx/UniverseStore.h"

#include <chrono>
#include <thread>

namespace dmxtest {

// Polls `condition` every few milliseconds until it is true or `timeout` passes.
template <typename Condition>
bool waitFor(Condition&& condition, std::chrono::milliseconds timeout = std::chrono::milliseconds(800)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return condition();
}

// Waits until the store holds universe `universe` with `value` at `address`.
inline bool waitForChannel(dmxviz::dmx::UniverseStore& store, dmxviz::dmx::UniverseId universe, std::uint16_t address,
                           std::uint8_t value) {
    dmxviz::dmx::DmxSnapshot snapshot;
    return waitFor([&] {
        store.snapshot(snapshot);
        return snapshot.universe(universe) != nullptr && snapshot.channel(universe, address) == value;
    });
}

}  // namespace dmxtest
