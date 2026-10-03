// Thread-safety stress tests for the DMX module. They are meaningful under ThreadSanitizer
// (the `linux-sanitizers` CI job) but also check data integrity in normal builds: every
// frame written by the threads is one repeated byte, so a torn read shows up as a frame
// whose bytes differ.
//
// Threading contract exercised here (see DmxManager.h): the main thread owns the interface
// list, start/stop and configuration; IO threads, the output thread, status monitors,
// the programmer and other writers run concurrently with it.

#include "../dmx/DmxTestUtil.h"
#include "TestEnv.h"

#include "dmx/DmxManager.h"
#include "dmx/interfaces/ArtNetInterface.h"
#include "dmx/interfaces/LoopbackInterface.h"
#include "dmx/interfaces/SacnInterface.h"
#include "dmx/platform/UdpSocket.h"
#include "dmx/protocol/ArtNet.h"
#include "dmx/protocol/Sacn.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <algorithm>
#include <thread>
#include <vector>

using namespace dmxviz::dmx;

namespace {

// DMXVIZ_STRESS_SCALE=10 makes the stress tests ten times longer (for TSan runs on a laptop).
int stressScale() {
    return std::max(1, dmxviz::robust::envInt("DMXVIZ_STRESS_SCALE", 1));
}

bool isUniform(const UniverseData& frame) {
    for (std::uint8_t b : frame)
        if (b != frame[0]) return false;
    return true;
}

UniverseData filled(std::uint8_t value) {
    UniverseData frame;
    frame.fill(value);
    return frame;
}

}  // namespace

TEST_CASE("robustness: UniverseStore survives concurrent writers, snapshots and merges") {
    UniverseStore store;
    std::atomic<bool> done{false};
    std::atomic<int> torn{0};
    std::atomic<int> finishedWriters{0};
    const int iterations = 1500 * stressScale();

    std::vector<std::thread> writers;
    for (std::uint32_t t = 0; t < 4; ++t) {
        writers.emplace_back([&, t] {
            SourceDescriptor source;
            source.id = SourceId::fromEndpoint(
                1 + t, Endpoint{Ipv4Address::fromOctets(10, 0, 0, static_cast<std::uint8_t>(t)), 6454});
            source.name = "writer";
            for (int i = 0; i < iterations; ++i) {
                const UniverseData frame = filled(static_cast<std::uint8_t>(1 + i % 250));
                store.submit(static_cast<UniverseId>(1 + i % 3), source, frame);
                if (i % 97 == 0) store.removeSource(static_cast<UniverseId>(1 + i % 3), source.id);
                if (i % 211 == 0) store.removeInterfaceSources(1 + t);
            }
            ++finishedWriters;
        });
    }
    // Settings and the programmer change while the writers run. Universe 50 is the
    // programmer's own, so the uniform-frame check below stays valid for universes 1..3.
    std::thread settings([&] {
        for (int i = 0; !done.load(); ++i) {
            store.setSourceTimeout(std::chrono::milliseconds(100 + i % 50));
            store.setHoldLastLook(i % 2 == 0);
            store.setProgrammerMode(i % 3 == 0 ? ProgrammerMode::Override : ProgrammerMode::Merge);
            store.setProgrammerChannel(50, static_cast<std::uint16_t>(1 + i % 512), static_cast<std::uint8_t>(i));
            if (i % 40 == 0) store.clearProgrammerUniverse(50);
            std::this_thread::yield();
        }
    });
    std::thread merger([&] {
        UniverseData out;
        while (!done.load()) {
            for (UniverseId u = 1; u <= 3; ++u) {
                store.mergeForOutput(u, 99, out);
                if (!isUniform(out)) ++torn;
            }
        }
    });

    DmxSnapshot snapshot;
    while (finishedWriters.load() < 4) {
        store.snapshot(snapshot);
        for (UniverseId u = 1; u <= 3; ++u)
            if (const UniverseData* frame = snapshot.universe(u))
                if (!isUniform(*frame)) ++torn;
        static_cast<void>(store.programmerValues(50));
    }
    for (std::thread& w : writers) w.join();
    done = true;
    settings.join();
    merger.join();

    CHECK(torn.load() == 0);
}

TEST_CASE("robustness: DmxManager with live interfaces, feeders and monitors while restarting") {
    DmxManager manager;
    manager.setOutputRate(DmxManager::kMaxOutputRate);

    const InterfaceId artnetId = manager.addInterface(ArtNetInterface::kTypeName);
    const InterfaceId sacnId = manager.addInterface(SacnInterface::kTypeName);
    const InterfaceId loopId = manager.addInterface(LoopbackInterface::kTypeName);
    REQUIRE(artnetId != 0);
    REQUIRE(sacnId != 0);
    REQUIRE(loopId != 0);

    // Both network interfaces talk to a plain UDP socket on loopback; nothing leaves the machine.
    UdpSocket peer;
    std::string error;
    REQUIRE(peer.open(Endpoint{kLoopbackAddress, 0}, false, error));
    const Endpoint peerEndpoint = peer.localEndpoint();

    auto* artnet = manager.findInterfaceAs<ArtNetInterface>(artnetId);
    auto* sacnIf = manager.findInterfaceAs<SacnInterface>(sacnId);
    auto* loop = manager.findInterfaceAs<LoopbackInterface>(loopId);
    REQUIRE(artnet);
    REQUIRE(sacnIf);
    REQUIRE(loop);
    {
        ArtNetConfig c = artnet->config();
        c.nic = kLoopbackAddress;
        c.port = 0;
        c.broadcastOutput = false;
        c.unicastTargets = {peerEndpoint};
        artnet->setConfig(std::move(c));
        SacnConfig s = sacnIf->config();
        s.nic = kLoopbackAddress;
        s.port = 0;
        s.multicastInput = false;
        s.multicastOutput = false;
        s.acceptAllUniverses = true;
        s.unicastTargets = {peerEndpoint};
        sacnIf->setConfig(std::move(s));
    }
    manager.setRoutes({{1, artnetId}, {1, sacnId}, {2, loopId}, {2, artnetId}});

    std::atomic<bool> done{false};
    std::atomic<int> torn{0};

    // Network feeders: Art-Net and sACN frames at whatever port the interfaces currently use.
    std::thread feeder([&] {
        UdpSocket socket;
        std::string socketError;
        if (!socket.open(Endpoint{kLoopbackAddress, 0}, false, socketError)) return;
        std::array<std::uint8_t, 1024> buffer{};
        const sacn::Cid cid = sacn::generateCid();
        std::uint8_t sequence = 0;
        while (!done.load()) {
            ++sequence;
            artnet::DmxPacket a;
            a.sequence = sequence;
            a.portAddress = static_cast<std::uint16_t>(sequence % 4);
            const UniverseData frame = filled(sequence);
            a.data = frame;
            if (const std::uint16_t port = artnet->boundPort(); port != 0) {
                const std::size_t n = artnet::encodeDmx(a, buffer);
                socket.sendTo(std::span(buffer).first(n), Endpoint{kLoopbackAddress, port});
            }
            sacn::DataPacket s;
            s.cid = cid;
            s.universe = static_cast<std::uint16_t>(1 + sequence % 4);
            s.sequence = sequence;
            s.sourceName = "stress";
            s.slots = frame;
            if (const std::uint16_t port = sacnIf->boundPort(); port != 0) {
                const std::size_t n = sacn::encodeData(s, buffer);
                socket.sendTo(std::span(buffer).first(n), Endpoint{kLoopbackAddress, port});
            }
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
    });

    // Status monitor (the UI reads status() every frame) and input toggling from other threads.
    std::thread monitor([&] {
        while (!done.load()) {
            for (DmxInterface* iface : {static_cast<DmxInterface*>(artnet), static_cast<DmxInterface*>(sacnIf),
                                        static_cast<DmxInterface*>(loop)}) {
                const InterfaceStatus status = iface->status();
                static_cast<void>(status.message.size());
                static_cast<void>(iface->label());
                iface->setInputEnabled(status.packetsIn % 7 != 0);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });
    std::thread programmer([&] {
        for (int i = 0; !done.load(); ++i) {
            manager.setProgrammerChannel(static_cast<UniverseId>(1 + i % 3), static_cast<std::uint16_t>(1 + i % 512),
                                         static_cast<std::uint8_t>(i));
            if (i % 50 == 0) manager.clearProgrammer();
            std::this_thread::yield();
        }
    });
    // Direct send() calls: the output thread is not the only sender (test console, tools).
    std::thread sender([&] {
        for (int i = 0; !done.load(); ++i) {
            const UniverseData frame = filled(static_cast<std::uint8_t>(i));
            artnet->send(static_cast<UniverseId>(1 + i % 4), frame);
            sacnIf->send(static_cast<UniverseId>(1 + i % 4), frame);
            loop->send(static_cast<UniverseId>(1 + i % 4), frame);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    // The main thread restarts interfaces, edits their configuration and takes snapshots.
    DmxSnapshot snapshot;
    const int cycles = 6 * stressScale();
    for (int i = 0; i < cycles; ++i) {
        for (InterfaceId id : {artnetId, sacnId, loopId}) {
            std::string startError;
            CHECK_MESSAGE(manager.startInterface(id, startError), startError);
        }
        for (int k = 0; k < 20; ++k) {
            manager.snapshot(snapshot);
            static_cast<void>(manager.saveConfig());
            loop->setLabel(k % 2 ? "loop a" : "loop b");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        manager.setRoutes(i % 2 ? std::vector<OutputRoute>{{1, artnetId}, {2, loopId}}
                                : std::vector<OutputRoute>{{1, sacnId}, {2, artnetId}, {3, loopId}});
        manager.setOutputEnabled(i % 3 != 0);
        for (InterfaceId id : {artnetId, sacnId, loopId}) manager.stopInterface(id);
        loop->setUniverseOffset(i % 3);  // edited while stopped; the output thread may still call send()
        manager.snapshot(snapshot);
    }

    done = true;
    feeder.join();
    monitor.join();
    programmer.join();
    sender.join();
    manager.stopAllInterfaces();
    CHECK(torn.load() == 0);

    // Removing and reloading with no other thread holding interface pointers.
    const nlohmann::json config = manager.saveConfig();
    std::string loadError;
    CHECK_MESSAGE(manager.loadConfig(config, loadError), loadError);
    CHECK(manager.removeInterface(manager.interfaces().front()->id()));
}
