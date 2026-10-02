// sACN interface end to end over UDP unicast on 127.0.0.1 (multicast may be unavailable
// in CI containers, so nothing here depends on it).

#include "DmxTestUtil.h"

#include "dmx/interfaces/SacnInterface.h"
#include "dmx/platform/UdpSocket.h"
#include "dmx/protocol/Sacn.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <chrono>

using namespace dmxviz::dmx;
using namespace std::chrono_literals;

namespace {

SacnConfig loopbackConfig() {
    SacnConfig c;
    c.nic = kLoopbackAddress;
    c.port = 0;
    c.multicastInput = false;
    c.multicastOutput = false;
    return c;
}

struct SacnReceiver {
    UniverseStore store;
    SacnInterface iface;

    explicit SacnReceiver(SacnConfig config = loopbackConfig()) {
        iface.setConfig(std::move(config));
        iface.attach(&store, 1);
        std::string error;
        REQUIRE_MESSAGE(iface.start(error), error);
    }
    Endpoint endpoint() const { return {kLoopbackAddress, iface.boundPort()}; }
};

// Sends hand-made E1.31 packets from a plain socket.
struct RawSender {
    UdpSocket socket;
    sacn::Cid cid = sacn::generateCid();

    RawSender() {
        std::string error;
        REQUIRE_MESSAGE(socket.open({kLoopbackAddress, 0}, false, error), error);
    }
    void send(const Endpoint& to, std::uint16_t universe, std::uint8_t sequence, std::uint8_t value,
              std::uint8_t startCode = 0, bool preview = false, bool terminated = false) {
        const std::array<std::uint8_t, 4> slots = {value, value, value, value};
        sacn::DataPacket p;
        p.cid = cid;
        p.sourceName = "Raw";
        p.universe = universe;
        p.sequence = sequence;
        p.startCode = startCode;
        p.preview = preview;
        p.streamTerminated = terminated;
        p.slots = slots;
        std::array<std::uint8_t, sacn::kMaxDataPacketSize> buffer{};
        const std::size_t n = sacn::encodeData(p, buffer);
        REQUIRE(socket.sendTo(std::span(buffer).first(n), to));
    }
};

}  // namespace

TEST_CASE("sacn interface: unicast send and receive with priority and name") {
    SacnReceiver rx;
    SacnInterface tx;
    SacnConfig tc = loopbackConfig();
    tc.sourceName = "Desk";
    tc.priority = 150;
    tc.unicastTargets = {rx.endpoint()};
    tx.setConfig(tc);
    std::string error;
    REQUIRE_MESSAGE(tx.start(error), error);

    UniverseData data{};
    data[0] = 10;
    data[511] = 20;
    tx.send(1, data);
    REQUIRE(dmxtest::waitForChannel(rx.store, 1, 512, 20));

    DmxSnapshot snap;
    rx.store.snapshot(snap);
    CHECK(snap.channel(1, 1) == 10);
    const UniverseInfo* info = snap.info(1);
    REQUIRE(info);
    REQUIRE(info->sources.size() == 1);
    CHECK(info->sources[0].name == "Desk");
    CHECK(info->sources[0].priority == 150);
    CHECK(info->sources[0].protocol == Protocol::Sacn);
    CHECK(info->sources[0].endpoint.address == kLoopbackAddress);

    // Stopping the sender sends "stream terminated": the source disappears immediately
    // (well before the 2.5 s timeout), even though hold-last-look is on.
    tx.stop();
    CHECK(dmxtest::waitFor([&] {
        rx.store.snapshot(snap);
        return snap.universe(1) == nullptr;
    }));
}

TEST_CASE("sacn interface: universe filter, offset and accept-all") {
    SacnConfig c = loopbackConfig();
    c.universes = {5};
    c.universeOffset = 100;
    SacnReceiver rx(c);
    RawSender raw;

    raw.send(rx.endpoint(), 6, 1, 66);  // not subscribed: ignored
    raw.send(rx.endpoint(), 5, 1, 55);  // subscribed: logical universe 105
    REQUIRE(dmxtest::waitForChannel(rx.store, 105, 1, 55));
    DmxSnapshot snap;
    rx.store.snapshot(snap);
    CHECK(snap.universe(106) == nullptr);

    SacnConfig all = loopbackConfig();
    all.acceptAllUniverses = true;
    SacnReceiver rxAll(all);
    raw.send(rxAll.endpoint(), 4000, 1, 77);
    CHECK(dmxtest::waitForChannel(rxAll.store, 4000, 1, 77));
}

TEST_CASE("sacn interface: out-of-order packets are dropped") {
    SacnReceiver rx;
    RawSender raw;
    raw.send(rx.endpoint(), 1, 10, 100);
    REQUIRE(dmxtest::waitForChannel(rx.store, 1, 1, 100));
    raw.send(rx.endpoint(), 1, 9, 90);   // late: dropped
    raw.send(rx.endpoint(), 1, 10, 91);  // duplicate: dropped
    REQUIRE(dmxtest::waitFor([&] { return rx.iface.status().packetsInvalid >= 2; }));
    raw.send(rx.endpoint(), 1, 11, 110);
    REQUIRE(dmxtest::waitForChannel(rx.store, 1, 1, 110));
}

TEST_CASE("sacn interface: start codes, preview and termination") {
    SacnConfig c = loopbackConfig();
    c.acceptPreview = false;
    SacnReceiver rx(c);
    RawSender raw;

    raw.send(rx.endpoint(), 1, 1, 200, 0xDD);                // per-address priority: not levels
    raw.send(rx.endpoint(), 1, 2, 150, 0, /*preview*/ true);  // preview not wanted here
    REQUIRE(dmxtest::waitFor([&] { return rx.iface.status().packetsIn >= 2; }));
    DmxSnapshot snap;
    rx.store.snapshot(snap);
    CHECK(snap.universe(1) == nullptr);

    raw.send(rx.endpoint(), 1, 3, 30);
    REQUIRE(dmxtest::waitForChannel(rx.store, 1, 1, 30));
    raw.send(rx.endpoint(), 1, 4, 0, 0, false, /*terminated*/ true);
    CHECK(dmxtest::waitFor([&] {
        rx.store.snapshot(snap);
        return snap.universe(1) == nullptr;
    }));
}

TEST_CASE("sacn interface: our own CID is ignored") {
    SacnReceiver rx;
    RawSender raw;
    raw.cid = rx.iface.config().cid;  // pretend to be the receiver itself
    raw.send(rx.endpoint(), 1, 1, 42);
    std::this_thread::sleep_for(30ms);
    DmxSnapshot snap;
    rx.store.snapshot(snap);
    CHECK(snap.universe(1) == nullptr);
}

TEST_CASE("sacn interface: failed multicast join does not stop unicast") {
    SacnConfig c = loopbackConfig();
    c.multicastInput = true;  // may or may not work on loopback; start() must succeed either way
    c.universes = {1, 2};
    SacnReceiver rx(c);
    CHECK(rx.iface.running());
    RawSender raw;
    raw.send(rx.endpoint(), 2, 1, 9);
    CHECK(dmxtest::waitForChannel(rx.store, 2, 1, 9));
}

TEST_CASE("sacn interface: config JSON round trip keeps the CID") {
    SacnInterface a;
    SacnConfig c = a.config();
    c.nic = Ipv4Address::fromOctets(10, 0, 0, 2);
    c.universes = {1, 2, 300};
    c.universeOffset = 4;
    c.priority = 180;
    c.sourceName = "FOH";
    c.multicastOutput = false;
    c.unicastTargets = {Endpoint{Ipv4Address::fromOctets(10, 0, 0, 9), 5568}};
    c.acceptPreview = false;
    a.setConfig(c);

    SacnInterface b;
    CHECK(b.config().cid != a.config().cid);  // every interface starts with its own CID
    std::string error;
    REQUIRE_MESSAGE(b.loadConfig(a.saveConfig(), error), error);
    CHECK(b.config().cid == a.config().cid);
    CHECK(b.config().nic == c.nic);
    CHECK(b.config().universes == c.universes);
    CHECK(b.config().universeOffset == 4);
    CHECK(b.config().priority == 180);
    CHECK(b.config().sourceName == "FOH");
    CHECK_FALSE(b.config().multicastOutput);
    CHECK(b.config().unicastTargets == c.unicastTargets);
    CHECK_FALSE(b.config().acceptPreview);

    nlohmann::json bad = a.saveConfig();
    bad["universes"] = {0};
    CHECK_FALSE(b.loadConfig(bad, error));
    bad = a.saveConfig();
    bad["cid"] = "nope";
    CHECK_FALSE(b.loadConfig(bad, error));
}
