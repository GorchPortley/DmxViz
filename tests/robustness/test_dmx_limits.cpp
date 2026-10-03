// Resource limits of the DMX module: a flood of spoofed senders and hostile configuration
// values must neither grow memory without bound nor trigger undefined behaviour.

#include "dmx/DmxManager.h"
#include "dmx/UniverseStore.h"
#include "dmx/interfaces/ArtNetInterface.h"
#include "dmx/interfaces/LoopbackInterface.h"
#include "dmx/interfaces/SacnInterface.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <limits>

using namespace dmxviz::dmx;
using nlohmann::json;

namespace {

SourceId spoofedSource(std::uint32_t n) {
    std::array<std::uint8_t, 16> cid{};
    for (std::size_t i = 0; i < 4; ++i) cid[i] = static_cast<std::uint8_t>(n >> (8 * i));
    return SourceId::fromCid(1, cid);
}

std::size_t totalSources(UniverseStore& store) {
    DmxSnapshot snapshot;
    store.snapshot(snapshot);
    std::size_t total = 0;
    for (UniverseId id : snapshot.universes()) total += snapshot.info(id)->sources.size();
    return total;
}

}  // namespace

TEST_CASE("limits: one universe accepts a bounded number of distinct sources") {
    UniverseStore store;
    const std::array<std::uint8_t, 512> frame{};
    std::size_t accepted = 0;
    for (std::uint32_t n = 0; n < 1000; ++n) {
        SourceDescriptor source;
        source.id = spoofedSource(n);
        if (store.submit(7, source, frame)) ++accepted;
    }
    CHECK(accepted == UniverseStore::kMaxSourcesPerUniverse);
    CHECK(totalSources(store) == UniverseStore::kMaxSourcesPerUniverse);

    // A known source keeps working while the universe is full.
    SourceDescriptor known;
    known.id = spoofedSource(3);
    CHECK(store.submit(7, known, frame));
}

TEST_CASE("limits: the store as a whole accepts a bounded number of sources, and recovers after they expire") {
    UniverseStore store;
    store.setHoldLastLook(true);
    const std::array<std::uint8_t, 512> frame{};
    const TimePoint start = Clock::now();
    std::uint32_t n = 0;
    for (UniverseId universe = 1; universe <= 100; ++universe) {
        for (std::size_t k = 0; k < UniverseStore::kMaxSourcesPerUniverse; ++k) {
            SourceDescriptor source;
            source.id = spoofedSource(n++);
            store.submit(universe, source, frame, start);
        }
    }
    CHECK(totalSources(store) <= UniverseStore::kMaxSources);

    // Much later every flooding source has timed out (and is merely "held"); a real sender on a
    // new universe must get through again.
    SourceDescriptor real;
    real.id = spoofedSource(0xFFFFFF);
    CHECK(store.submit(500, real, frame, start + std::chrono::seconds(30)));
}

TEST_CASE("limits: hostile DMX configuration is rejected or clamped, never undefined") {
    DmxManager manager(DmxManagerOptions{false});
    std::string error;

    auto interfaceWith = [](const json& settings) {
        return json{{"interfaces", json::array({json{{"id", 1}, {"type", "Art-Net"}, {"settings", settings}}})}};
    };

    CHECK_FALSE(manager.loadConfig(interfaceWith({{"universeOffset", 1e300}}), error));
    CHECK_FALSE(manager.loadConfig(interfaceWith({{"universeOffset", 2147483647}}), error));
    CHECK_FALSE(manager.loadConfig(interfaceWith({{"port", -1e300}}), error));
    CHECK_FALSE(manager.loadConfig(interfaceWith({{"port", "abc"}}), error));
    CHECK_FALSE(manager.loadConfig(json{{"formatVersion", 1e300}}, error));
    CHECK_FALSE(
        manager.loadConfig(json{{"interfaces", json::array({json{{"id", 1e300}, {"type", "Art-Net"}}})}}, error));
    CHECK_FALSE(
        manager.loadConfig(json{{"routes", json::array({json{{"universe", 1e300}, {"interface", 1}}})}}, error));

    // Absurdly many interfaces or routes.
    json many = json::array();
    for (int i = 1; i <= 300; ++i) many.push_back(json{{"id", i}, {"type", "Loopback"}});
    CHECK_FALSE(manager.loadConfig(json{{"interfaces", many}}, error));
    CHECK(error.find("more than") != std::string::npos);
    json routes = json::array();
    for (int i = 0; i < 70000; ++i) routes.push_back(json{{"universe", 1 + i % 60000}, {"interface", 1}});
    CHECK_FALSE(manager.loadConfig(json{{"routes", routes}}, error));

    // Huge floats where a clamp is applied are fine and clamp.
    CHECK(manager.loadConfig(json{{"sourceTimeoutMs", 1e300}, {"outputRateHz", 1e300}}, error));
    CHECK(manager.store().sourceTimeout().count() == 60000);
    CHECK(manager.outputRate() == DmxManager::kMaxOutputRate);

    // Out-of-range numbers set from code are harmless too.
    manager.setOutputRate(std::numeric_limits<double>::quiet_NaN());
    CHECK(manager.outputRate() == DmxManager::kMaxOutputRate);
    LoopbackInterface loopback;
    loopback.setUniverseOffset(std::numeric_limits<int>::max());
    CHECK(loopback.universeOffset() == 0xFFFF);
}

TEST_CASE("limits: oversized interface settings lists are rejected") {
    SacnInterface sacn;
    ArtNetInterface artnet;
    std::string error;
    json universes = json::array();
    for (int i = 0; i < 5000; ++i) universes.push_back(1 + i % 60000);
    CHECK_FALSE(sacn.loadConfig(json{{"universes", universes}}, error));
    CHECK_FALSE(artnet.loadConfig(json{{"announcedUniverses", universes}}, error));
    json targets = json::array();
    for (int i = 0; i < 5000; ++i) targets.push_back("127.0.0.1:6454");
    CHECK_FALSE(artnet.loadConfig(json{{"unicastTargets", targets}}, error));
    CHECK_FALSE(sacn.loadConfig(json{{"priority", 1e300}}, error));
}
