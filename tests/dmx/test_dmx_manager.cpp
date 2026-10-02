// DmxManager: interface registry and lifecycle, output routing (on change + keep-alive,
// no echo), programmer, JSON configuration, and two managers talking Art-Net.

#include "DmxTestUtil.h"

#include "dmx/DmxManager.h"
#include "dmx/interfaces/ArtNetInterface.h"
#include "dmx/interfaces/LoopbackInterface.h"
#include "dmx/interfaces/SacnInterface.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <vector>

using namespace dmxviz::dmx;
using namespace std::chrono_literals;

namespace {

const TimePoint t0 = TimePoint{} + 5000s;

// Output-only interface that records what it is asked to send.
class RecordingOutput : public DmxInterface {
public:
    struct Sent {
        UniverseId universe;
        UniverseData data;
    };
    std::vector<Sent> sent;

    std::string typeName() const override { return "Recorder"; }
    Capabilities caps() const override { return {false, true}; }
    bool start(std::string&) override {
        setRunning("recording");
        return true;
    }
    void stop() override { setStopped(); }
    void send(UniverseId universe, const UniverseData& data) override { sent.push_back({universe, data}); }
    nlohmann::json saveConfig() const override { return nlohmann::json::object(); }
    bool loadConfig(const nlohmann::json&, std::string&) override { return true; }
};

DmxManagerOptions noThread() {
    return DmxManagerOptions{false};
}

}  // namespace

TEST_CASE("manager: registry and interface lifecycle") {
    DmxManager manager(noThread());
    std::vector<std::string> names;
    for (const InterfaceTypeInfo& type : manager.registry().types()) names.push_back(type.name);
    CHECK(names ==
          std::vector<std::string>{"Art-Net", "sACN", "Enttec DMX USB Pro", "Enttec Open DMX USB", "Loopback"});
    REQUIRE(manager.registry().find("Enttec Open DMX USB"));
    CHECK_FALSE(manager.registry().find("Enttec Open DMX USB")->caps.input);

    const InterfaceId a = manager.addInterface("Art-Net");
    const InterfaceId b = manager.addInterface("Loopback");
    CHECK(manager.addInterface("Carrier pigeon") == 0);
    CHECK(a == 1);
    CHECK(b == 2);
    REQUIRE(manager.interfaces().size() == 2);
    CHECK(manager.interfaces()[0]->label() == "Art-Net 1");
    CHECK(manager.findInterfaceAs<ArtNetInterface>(a) != nullptr);
    CHECK(manager.findInterfaceAs<ArtNetInterface>(b) == nullptr);
    CHECK_FALSE(manager.findInterface(a)->running());

    std::string error;
    REQUIRE(manager.startInterface(b, error));
    CHECK(manager.findInterface(b)->enabled());
    manager.stopInterface(b);
    CHECK_FALSE(manager.findInterface(b)->running());
    CHECK_FALSE(manager.findInterface(b)->enabled());
    CHECK_FALSE(manager.startInterface(99, error));

    manager.addRoute({1, b});
    CHECK(manager.removeInterface(b));
    CHECK(manager.routes().empty());  // routes to a removed interface go too
    CHECK_FALSE(manager.removeInterface(b));
    CHECK(manager.interfaces().size() == 1);
}

TEST_CASE("manager: output on change plus one second keep-alive") {
    DmxManager manager(noThread());
    auto recorder = std::make_unique<RecordingOutput>();
    RecordingOutput* out = recorder.get();
    const InterfaceId id = manager.addInterface(std::move(recorder));
    std::string error;
    REQUIRE(manager.startInterface(id, error));
    manager.setRoutes({{1, id}, {2, id}, {1, id}});  // the duplicate is dropped
    CHECK(manager.routes().size() == 2);

    manager.setProgrammerChannel(1, 10, 200);
    manager.processOutput(t0);
    REQUIRE(out->sent.size() == 2);  // first refresh sends every route
    CHECK(out->sent[0].universe == 1);
    CHECK(out->sent[0].data[9] == 200);
    CHECK(out->sent[1].universe == 2);
    CHECK(out->sent[1].data[9] == 0);  // routed but empty universes go out as zeros

    manager.processOutput(t0 + 25ms);
    CHECK(out->sent.size() == 2);  // nothing changed

    manager.setProgrammerChannel(1, 10, 100);
    manager.processOutput(t0 + 50ms);
    REQUIRE(out->sent.size() == 3);  // only the changed universe
    CHECK(out->sent[2].universe == 1);
    CHECK(out->sent[2].data[9] == 100);

    manager.processOutput(t0 + 990ms);
    CHECK(out->sent.size() == 3);
    manager.processOutput(t0 + 1001ms);  // universe 2 was last sent at t0: keep-alive
    REQUIRE(out->sent.size() == 4);
    CHECK(out->sent[3].universe == 2);
    manager.processOutput(t0 + 1051ms);  // universe 1 was last sent at t0 + 50 ms
    CHECK(out->sent.size() == 5);

    manager.setOutputEnabled(false);
    manager.setProgrammerChannel(1, 1, 1);
    manager.processOutput(t0 + 3s);
    CHECK(out->sent.size() == 5);
    manager.setOutputEnabled(true);

    manager.stopInterface(id);  // stopped interfaces get nothing...
    manager.processOutput(t0 + 4s);
    CHECK(out->sent.size() == 5);
    REQUIRE(manager.startInterface(id, error));
    manager.processOutput(t0 + 4s + 1ms);  // ...and everything as soon as they are back
    CHECK(out->sent.size() == 7);
}

TEST_CASE("manager: loopback routing and programmer modes") {
    DmxManager manager(noThread());
    const InterfaceId loop = manager.addInterface("Loopback");
    manager.findInterfaceAs<LoopbackInterface>(loop)->setUniverseOffset(100);
    std::string error;
    REQUIRE(manager.startInterface(loop, error));
    manager.addRoute({1, loop});

    manager.setProgrammerChannel(1, 1, 77);
    manager.processOutput(t0);
    DmxSnapshot snap;
    manager.snapshot(snap);
    CHECK(snap.channel(1, 1) == 77);    // the programmer itself
    CHECK(snap.channel(101, 1) == 77);  // looped back with offset
    REQUIRE(snap.info(101));
    CHECK(snap.info(101)->sources[0].protocol == Protocol::Loopback);
    CHECK(snap.info(101)->sources[0].interfaceId == loop);

    manager.setProgrammerMode(ProgrammerMode::Override);
    CHECK(manager.programmerMode() == ProgrammerMode::Override);
    REQUIRE(manager.programmerValues(1));
    CHECK((*manager.programmerValues(1))[0] == 77);
    manager.clearProgrammer();
    CHECK_FALSE(manager.programmerValues(1));
}

TEST_CASE("manager: an interface never gets its own input echoed back") {
    // Loopback with offset 0 feeds universe 1 back into universe 1. Without the echo rule
    // HTP would latch the highest value ever sent.
    DmxManager manager(noThread());
    const InterfaceId loop = manager.addInterface("Loopback");
    std::string error;
    REQUIRE(manager.startInterface(loop, error));
    manager.addRoute({1, loop});

    manager.setProgrammerChannel(1, 1, 200);
    manager.processOutput(t0);
    manager.setProgrammerChannel(1, 1, 10);
    manager.processOutput(t0 + 30ms);
    DmxSnapshot snap;
    manager.snapshot(snap, t0 + 30ms);
    CHECK(snap.channel(1, 1) == 10);
}

TEST_CASE("manager: output thread refreshes routes on its own") {
    DmxManager manager;           // with the thread
    manager.setOutputRate(1000);  // clamped to 44 Hz
    CHECK(manager.outputRate() == DmxManager::kMaxOutputRate);
    const InterfaceId loop = manager.addInterface("Loopback");
    manager.findInterfaceAs<LoopbackInterface>(loop)->setUniverseOffset(10);
    std::string error;
    REQUIRE(manager.startInterface(loop, error));
    manager.addRoute({1, loop});
    manager.setProgrammerChannel(1, 5, 55);
    CHECK(dmxtest::waitForChannel(manager.store(), 11, 5, 55));
}

TEST_CASE("manager: configuration JSON round trip") {
    DmxManager a(noThread());
    const InterfaceId art = a.addInterface("Art-Net");
    const InterfaceId sacnId = a.addInterface("sACN");
    const InterfaceId pro = a.addInterface("Enttec DMX USB Pro");
    const InterfaceId loop = a.addInterface("Loopback");
    ArtNetConfig artConfig;
    artConfig.nic = Ipv4Address::fromOctets(2, 0, 0, 1);
    artConfig.universeOffset = 3;
    a.findInterfaceAs<ArtNetInterface>(art)->setConfig(artConfig);
    a.findInterface(sacnId)->setLabel("FOH desk");
    a.findInterface(sacnId)->setInputEnabled(false);
    a.findInterface(loop)->setEnabled(true);
    a.setRoutes({{1, art}, {2, sacnId}, {7, loop}});
    a.setOutputRate(30);
    a.setOutputEnabled(false);
    a.setProgrammerMode(ProgrammerMode::Override);
    a.store().setSourceTimeout(4000ms);
    a.store().setHoldLastLook(false);

    const nlohmann::json saved = a.saveConfig();
    DmxManager b(noThread());
    b.addInterface("Loopback");  // replaced by the load
    std::string error;
    REQUIRE_MESSAGE(b.loadConfig(saved, error), error);
    CHECK(b.saveConfig() == saved);

    REQUIRE(b.interfaces().size() == 4);
    CHECK(b.findInterfaceAs<ArtNetInterface>(art)->config().nic == artConfig.nic);
    CHECK(b.findInterfaceAs<ArtNetInterface>(art)->config().universeOffset == 3);
    CHECK(b.findInterface(sacnId)->label() == "FOH desk");
    CHECK_FALSE(b.findInterface(sacnId)->inputEnabled());
    CHECK(b.findInterfaceAs<SacnInterface>(sacnId)->config().cid ==
          a.findInterfaceAs<SacnInterface>(sacnId)->config().cid);
    CHECK(b.findInterface(pro)->typeName() == "Enttec DMX USB Pro");
    CHECK(b.routes().size() == 3);
    CHECK(b.outputRate() == 30.0);
    CHECK_FALSE(b.outputEnabled());
    CHECK(b.programmerMode() == ProgrammerMode::Override);
    CHECK(b.store().sourceTimeout() == 4000ms);
    CHECK_FALSE(b.store().holdLastLook());
    CHECK(b.addInterface("Loopback") == 5);  // ids continue after the loaded ones

    // Only interfaces marked enabled start (the loopback).
    DmxManager c(noThread());
    REQUIRE_MESSAGE(c.loadConfig(saved, error), error);
    CHECK(c.startEnabledInterfaces() == 0);
    CHECK(c.findInterface(loop)->running());
    CHECK_FALSE(c.findInterface(art)->running());
}

TEST_CASE("manager: invalid configuration leaves the current setup untouched") {
    DmxManager manager(noThread());
    const InterfaceId loop = manager.addInterface("Loopback");
    manager.addRoute({1, loop});
    const nlohmann::json before = manager.saveConfig();
    std::string error;

    nlohmann::json bad = before;
    bad["routes"] = {{{"universe", 0}, {"interface", loop}}};
    CHECK_FALSE(manager.loadConfig(bad, error));
    CHECK_FALSE(error.empty());

    bad = before;
    bad["programmerMode"] = "loudest";
    CHECK_FALSE(manager.loadConfig(bad, error));

    bad = before;
    bad["interfaces"][0]["settings"] = {{"universeOffset", "ten"}};
    CHECK_FALSE(manager.loadConfig(bad, error));

    bad = before;
    bad["interfaces"].push_back(bad["interfaces"][0]);  // duplicate id
    CHECK_FALSE(manager.loadConfig(bad, error));

    CHECK_FALSE(manager.loadConfig(nlohmann::json::array(), error));
    CHECK_FALSE(manager.loadConfig({{"formatVersion", 99}}, error));
    CHECK(manager.saveConfig() == before);

    // Unknown interface types are skipped (with their routes), not fatal.
    nlohmann::json future = before;
    future["interfaces"].push_back({{"id", 9}, {"type", "Art-Net 7"}, {"settings", nlohmann::json::object()}});
    future["routes"].push_back({{"universe", 3}, {"interface", 9}});
    REQUIRE_MESSAGE(manager.loadConfig(future, error), error);
    CHECK(manager.interfaces().size() == 1);
    CHECK(manager.routes().size() == 1);
}

TEST_CASE("manager: two managers talk Art-Net over 127.0.0.1") {
    // Receiver side
    DmxManager visualiser(noThread());
    const InterfaceId in = visualiser.addInterface("Art-Net");
    ArtNetConfig rc;
    rc.nic = kLoopbackAddress;
    rc.port = 0;
    visualiser.findInterfaceAs<ArtNetInterface>(in)->setConfig(rc);
    std::string error;
    REQUIRE_MESSAGE(visualiser.startInterface(in, error), error);
    const std::uint16_t port = visualiser.findInterfaceAs<ArtNetInterface>(in)->boundPort();

    // Console side: programmer -> route -> Art-Net unicast
    DmxManager console(noThread());
    const InterfaceId out = console.addInterface("Art-Net");
    ArtNetConfig sc;
    sc.nic = kLoopbackAddress;
    sc.port = 0;
    sc.broadcastOutput = false;
    sc.unicastTargets = {Endpoint{kLoopbackAddress, port}};
    console.findInterfaceAs<ArtNetInterface>(out)->setConfig(sc);
    REQUIRE_MESSAGE(console.startInterface(out, error), error);
    console.addRoute({2, out});
    console.setProgrammerChannel(2, 100, 123);
    console.processOutput();

    REQUIRE(dmxtest::waitForChannel(visualiser.store(), 2, 100, 123));
    DmxSnapshot snap;
    visualiser.snapshot(snap);
    REQUIRE(snap.info(2));
    CHECK(snap.info(2)->sources[0].interfaceId == in);
}
