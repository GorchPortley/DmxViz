// UniverseStore merge rules (priority, HTP, timeout, hold, terminate, programmer) and
// DmxSnapshot accessors. Time is injected, so nothing here sleeps.

#include "dmx/DmxSnapshot.h"
#include "dmx/UniverseStore.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace dmxviz::dmx;
using namespace std::chrono_literals;

namespace {

const TimePoint t0 = TimePoint{} + 1000s;  // any fixed origin works

SourceDescriptor artnetSource(InterfaceId interfaceId, std::uint8_t lastOctet) {
    SourceDescriptor d;
    d.endpoint = {Ipv4Address::fromOctets(10, 0, 0, lastOctet), 6454};
    d.id = SourceId::fromEndpoint(interfaceId, d.endpoint);
    d.protocol = Protocol::ArtNet;
    d.name = "node";
    return d;
}

SourceDescriptor sacnSource(InterfaceId interfaceId, std::uint8_t cidByte, std::uint8_t priority) {
    std::array<std::uint8_t, 16> cid{};
    cid[0] = cidByte;
    SourceDescriptor d;
    d.id = SourceId::fromCid(interfaceId, cid);
    d.protocol = Protocol::Sacn;
    d.priority = priority;
    d.name = "console";
    return d;
}

std::vector<std::uint8_t> frame(std::initializer_list<std::uint8_t> values) {
    return values;
}

}  // namespace

TEST_CASE("store: single source and snapshot accessors") {
    UniverseStore store;
    DmxSnapshot snap;
    store.snapshot(snap, t0);
    CHECK(snap.empty());
    CHECK(snap.universe(1) == nullptr);
    CHECK(snap.channel(1, 1) == 0);

    store.submit(3, artnetSource(1, 5), frame({10, 20, 30}), t0);
    store.snapshot(snap, t0 + 10ms);
    REQUIRE(snap.universes().size() == 1);
    CHECK(snap.universes()[0] == 3);
    REQUIRE(snap.universe(3) != nullptr);
    CHECK(snap.channel(3, 1) == 10);
    CHECK(snap.channel(3, 3) == 30);
    CHECK(snap.channel(3, 4) == 0);  // short frames are zero-filled
    CHECK(snap.channel(3, 0) == 0);  // invalid addresses read 0
    CHECK(snap.channel(3, 513) == 0);
    CHECK(snap.channel(2, 1) == 0);
    CHECK(snap.time() == t0 + 10ms);

    const UniverseInfo* info = snap.info(3);
    REQUIRE(info);
    CHECK(info->lastUpdate == t0);
    CHECK_FALSE(info->held);
    REQUIRE(info->sources.size() == 1);
    CHECK(info->sources[0].name == "node");
    CHECK(info->sources[0].endpoint.toString() == "10.0.0.5:6454");
    CHECK(info->sources[0].protocol == Protocol::ArtNet);
    CHECK(info->sources[0].interfaceId == 1);
    CHECK(snap.info(4) == nullptr);

    // Universe 0 is invalid and ignored.
    store.submit(0, artnetSource(1, 5), frame({1}), t0);
    store.snapshot(snap, t0);
    CHECK(snap.universes().size() == 1);
}

TEST_CASE("store: universes are listed in ascending order") {
    UniverseStore store;
    for (UniverseId u : {UniverseId{9}, UniverseId{2}, UniverseId{64}, UniverseId{5}})
        store.submit(u, artnetSource(1, 1), frame({static_cast<std::uint8_t>(u)}), t0);
    DmxSnapshot snap;
    store.snapshot(snap, t0);
    REQUIRE(snap.universes().size() == 4);
    CHECK(snap.universes()[0] == 2);
    CHECK(snap.universes()[3] == 64);
    CHECK(snap.channel(64, 1) == 64);
}

TEST_CASE("store: equal priority merges HTP") {
    UniverseStore store;
    store.submit(1, artnetSource(1, 1), frame({100, 0, 50}), t0);
    store.submit(1, artnetSource(1, 2), frame({20, 200, 60}), t0);
    DmxSnapshot snap;
    store.snapshot(snap, t0);
    CHECK(snap.channel(1, 1) == 100);
    CHECK(snap.channel(1, 2) == 200);
    CHECK(snap.channel(1, 3) == 60);
    CHECK(snap.info(1)->sources.size() == 2);

    // A newer frame from the same sender replaces its old one.
    store.submit(1, artnetSource(1, 1), frame({0, 0, 0}), t0 + 20ms);
    store.snapshot(snap, t0 + 20ms);
    CHECK(snap.channel(1, 1) == 20);
    CHECK(snap.info(1)->sources.size() == 2);
}

TEST_CASE("store: highest priority wins, lower takes over after timeout") {
    UniverseStore store;
    store.submit(1, sacnSource(1, 1, 100), frame({255, 255}), t0);
    store.submit(1, sacnSource(1, 2, 150), frame({10, 0}), t0);
    DmxSnapshot snap;
    store.snapshot(snap, t0);
    CHECK(snap.channel(1, 1) == 10);  // priority beats value
    CHECK(snap.channel(1, 2) == 0);

    // Keep the low-priority source alive; let the high one go silent.
    store.submit(1, sacnSource(1, 1, 100), frame({255, 255}), t0 + 2s);
    store.snapshot(snap, t0 + 2400ms);
    CHECK(snap.channel(1, 1) == 10);  // 2.4 s: still within the timeout
    store.submit(1, sacnSource(1, 1, 100), frame({255, 255}), t0 + 2500ms);
    store.snapshot(snap, t0 + 2600ms);
    CHECK(snap.channel(1, 1) == 255);
    CHECK(snap.info(1)->sources.size() == 1);
    CHECK_FALSE(snap.info(1)->held);
}

TEST_CASE("store: timeout with hold last look") {
    UniverseStore store;
    REQUIRE(store.holdLastLook());
    store.submit(1, artnetSource(1, 1), frame({77}), t0);
    DmxSnapshot snap;

    store.snapshot(snap, t0 + 3s);  // the only sender timed out
    REQUIRE(snap.universe(1) != nullptr);
    CHECK(snap.channel(1, 1) == 77);
    CHECK(snap.info(1)->held);
    CHECK(snap.info(1)->sources[0].held);
    CHECK(snap.info(1)->sources[0].packetsPerSecond == 0.0f);

    // Any fresh data replaces the held look entirely.
    store.submit(1, artnetSource(1, 2), frame({0, 5}), t0 + 4s);
    store.snapshot(snap, t0 + 4s);
    CHECK(snap.channel(1, 1) == 0);
    CHECK(snap.channel(1, 2) == 5);
    CHECK_FALSE(snap.info(1)->held);
    CHECK(snap.info(1)->sources.size() == 1);
}

TEST_CASE("store: timeout without hold releases the universe") {
    UniverseStore store;
    store.setHoldLastLook(false);
    store.submit(1, artnetSource(1, 1), frame({77}), t0);
    DmxSnapshot snap;
    store.snapshot(snap, t0 + 2s);
    CHECK(snap.channel(1, 1) == 77);
    store.snapshot(snap, t0 + 3s);
    CHECK(snap.universe(1) == nullptr);
    CHECK(snap.channel(1, 1) == 0);
}

TEST_CASE("store: turning hold off releases held universes") {
    UniverseStore store;
    store.submit(1, artnetSource(1, 1), frame({77}), t0);
    DmxSnapshot snap;
    store.snapshot(snap, t0 + 3s);
    CHECK(snap.channel(1, 1) == 77);
    store.setHoldLastLook(false);
    store.snapshot(snap, t0 + 3s);
    CHECK(snap.universe(1) == nullptr);
}

TEST_CASE("store: configurable timeout") {
    UniverseStore store;
    store.setHoldLastLook(false);
    store.setSourceTimeout(500ms);
    CHECK(store.sourceTimeout() == 500ms);
    store.submit(1, artnetSource(1, 1), frame({1}), t0);
    DmxSnapshot snap;
    store.snapshot(snap, t0 + 600ms);
    CHECK(snap.universe(1) == nullptr);
}

TEST_CASE("store: stream terminated removes a source immediately") {
    UniverseStore store;
    const auto a = sacnSource(1, 1, 100);
    const auto b = sacnSource(1, 2, 120);
    store.submit(1, a, frame({50}), t0);
    store.submit(1, b, frame({200}), t0);
    DmxSnapshot snap;
    store.snapshot(snap, t0);
    CHECK(snap.channel(1, 1) == 200);

    store.removeSource(1, b.id);
    store.snapshot(snap, t0);
    CHECK(snap.channel(1, 1) == 50);

    // Terminating the last source releases the universe even with hold enabled.
    store.removeSource(1, a.id);
    store.snapshot(snap, t0);
    CHECK(snap.universe(1) == nullptr);
}

TEST_CASE("store: stopping an interface drops its sources, held ones included") {
    UniverseStore store;
    store.submit(1, artnetSource(1, 1), frame({10}), t0);
    store.submit(1, artnetSource(2, 1), frame({20}), t0);
    store.submit(2, artnetSource(2, 1), frame({30}), t0);
    store.removeInterfaceSources(2);
    DmxSnapshot snap;
    store.snapshot(snap, t0);
    CHECK(snap.channel(1, 1) == 10);
    CHECK(snap.universe(2) == nullptr);

    store.snapshot(snap, t0 + 5s);  // interface 1's source is now held
    CHECK(snap.channel(1, 1) == 10);
    store.removeInterfaceSources(1);
    store.snapshot(snap, t0 + 5s);
    CHECK(snap.universe(1) == nullptr);
}

TEST_CASE("store: programmer in merge mode is HTP at priority 100") {
    UniverseStore store;
    CHECK(store.programmerMode() == ProgrammerMode::Merge);
    store.submit(1, artnetSource(1, 1), frame({100, 100, 100}), t0);
    store.setProgrammerChannel(1, 1, 50);   // lower: input wins
    store.setProgrammerChannel(1, 2, 180);  // higher: programmer wins
    DmxSnapshot snap;
    store.snapshot(snap, t0);
    CHECK(snap.channel(1, 1) == 100);
    CHECK(snap.channel(1, 2) == 180);
    CHECK(snap.channel(1, 3) == 100);

    // A higher-priority network source beats the programmer in merge mode.
    store.submit(1, sacnSource(1, 9, 150), frame({1, 1, 1}), t0);
    store.snapshot(snap, t0);
    CHECK(snap.channel(1, 2) == 1);

    // Programmer-only universes work without any input and never time out.
    store.setProgrammerChannel(7, 512, 99);
    store.snapshot(snap, t0 + 1h);
    CHECK(snap.channel(7, 512) == 99);
    REQUIRE(snap.info(7));
    CHECK(snap.info(7)->sources[0].protocol == Protocol::Programmer);

    // Invalid addresses are ignored.
    store.setProgrammerChannel(7, 0, 1);
    store.setProgrammerChannel(7, 513, 1);
    store.setProgrammerChannel(0, 1, 1);
    const auto values = store.programmerValues(7);
    REQUIRE(values);
    CHECK((*values)[511] == 99);
    CHECK((*values)[0] == 0);
    CHECK_FALSE(store.programmerValues(8));
}

TEST_CASE("store: programmer override wins on touched channels only") {
    UniverseStore store;
    store.submit(1, sacnSource(1, 1, 200), frame({200, 200, 200}), t0);
    store.setProgrammerMode(ProgrammerMode::Override);
    store.setProgrammerChannel(1, 2, 0);  // override to zero
    DmxSnapshot snap;
    store.snapshot(snap, t0);
    CHECK(snap.channel(1, 1) == 200);  // untouched: input passes through
    CHECK(snap.channel(1, 2) == 0);
    CHECK(snap.info(1)->sources.back().priority == kOverridePriority);

    store.setProgrammerMode(ProgrammerMode::Merge);
    store.snapshot(snap, t0);
    CHECK(snap.channel(1, 2) == 200);  // back to HTP: priority 200 input wins anyway

    std::array<std::uint8_t, 512> all{};
    all.fill(7);
    store.setProgrammerMode(ProgrammerMode::Override);
    store.setProgrammerUniverse(1, all);  // touches every channel
    store.snapshot(snap, t0);
    CHECK(snap.channel(1, 1) == 7);
    CHECK(snap.channel(1, 512) == 7);

    store.clearProgrammerUniverse(1);
    store.snapshot(snap, t0);
    CHECK(snap.channel(1, 1) == 200);
}

TEST_CASE("store: clearing the programmer releases its universes") {
    UniverseStore store;
    store.setProgrammerChannel(1, 1, 255);
    store.setProgrammerChannel(2, 1, 255);
    store.submit(2, artnetSource(1, 1), frame({10}), t0);
    store.clearProgrammer();
    DmxSnapshot snap;
    store.snapshot(snap, t0);
    CHECK(snap.universe(1) == nullptr);
    CHECK(snap.channel(2, 1) == 10);
}

TEST_CASE("store: merge for output leaves out the receiving interface") {
    UniverseStore store;
    store.submit(1, artnetSource(1, 1), frame({100, 0}), t0);  // received on interface 1
    store.submit(1, artnetSource(2, 1), frame({0, 150}), t0);  // received on interface 2
    store.setProgrammerChannel(1, 3, 33);

    UniverseData out{};
    REQUIRE(store.mergeForOutput(1, 1, out, t0));
    CHECK(out[0] == 0);
    CHECK(out[1] == 150);
    CHECK(out[2] == 33);
    REQUIRE(store.mergeForOutput(1, 2, out, t0));
    CHECK(out[0] == 100);
    CHECK(out[1] == 0);

    out.fill(9);
    CHECK_FALSE(store.mergeForOutput(5, 1, out, t0));
    CHECK(out[0] == 0);
}

TEST_CASE("store: packet rate for the monitor") {
    UniverseStore store;
    const auto src = artnetSource(1, 1);
    for (int i = 0; i <= 44; ++i) store.submit(1, src, frame({1}), t0 + i * 25ms);  // 40 Hz for 1.1 s
    DmxSnapshot snap;
    store.snapshot(snap, t0 + 1100ms);
    CHECK(snap.info(1)->sources[0].packetsPerSecond == doctest::Approx(40.0).epsilon(0.05));
}

TEST_CASE("store: snapshot reuses its storage") {
    UniverseStore store;
    store.submit(1, artnetSource(1, 1), frame({1}), t0);
    store.submit(2, artnetSource(1, 1), frame({2}), t0);
    DmxSnapshot snap;
    store.snapshot(snap, t0);
    const UniverseData* first = snap.universe(1);
    const std::string* name = &snap.info(1)->sources[0].name;
    store.snapshot(snap, t0 + 10ms);
    CHECK(snap.universe(1) == first);  // same buffer, no reallocation
    CHECK(&snap.info(1)->sources[0].name == name);
}

TEST_CASE("store: concurrent submit and snapshot") {
    UniverseStore store;
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        std::array<std::uint8_t, 512> data{};
        std::uint8_t v = 0;
        while (!stop.load()) {
            data.fill(v++);
            for (UniverseId u = 1; u <= 8; ++u) store.submit(u, artnetSource(1, 1), data);
        }
    });
    DmxSnapshot snap;
    bool consistent = true;
    for (int i = 0; i < 2000; ++i) {
        store.snapshot(snap);
        for (UniverseId u : snap.universes()) {
            const UniverseData& d = *snap.universe(u);
            consistent = consistent && std::all_of(d.begin(), d.end(), [&](std::uint8_t x) { return x == d[0]; });
        }
    }
    stop = true;
    writer.join();
    CHECK(consistent);  // never a half-written frame
}
