// Art-Net interface end to end over UDP on 127.0.0.1 with unicast and random ports:
// one interface sends, another receives into a UniverseStore. No multicast/broadcast.

#include "DmxTestUtil.h"

#include "dmx/interfaces/ArtNetInterface.h"
#include "dmx/platform/UdpSocket.h"
#include "dmx/protocol/ArtNet.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <chrono>

using namespace dmxviz::dmx;
using namespace std::chrono_literals;

namespace {

ArtNetConfig loopbackConfig() {
    ArtNetConfig c;
    c.nic = kLoopbackAddress;
    c.port = 0;  // any free port
    c.broadcastOutput = false;
    return c;
}

// A receiver and a sender talking over 127.0.0.1.
struct ArtNetPair {
    UniverseStore store;
    ArtNetInterface receiver;
    ArtNetInterface sender;

    explicit ArtNetPair(int receiverOffset = 0) {
        ArtNetConfig rc = loopbackConfig();
        rc.universeOffset = receiverOffset;
        receiver.setConfig(rc);
        receiver.attach(&store, 1);
        std::string error;
        REQUIRE_MESSAGE(receiver.start(error), error);

        ArtNetConfig sc = loopbackConfig();
        sc.unicastTargets = {Endpoint{kLoopbackAddress, receiver.boundPort()}};
        sender.setConfig(sc);
        sender.attach(nullptr, 2);  // output only in this test
        REQUIRE_MESSAGE(sender.start(error), error);
    }
};

}  // namespace

TEST_CASE("artnet interface: unicast send and receive") {
    ArtNetPair pair;
    REQUIRE(pair.receiver.running());
    CHECK(pair.receiver.boundPort() != 0);

    UniverseData data{};
    data[0] = 11;
    data[511] = 99;
    pair.sender.send(1, data);  // universe 1 = port-address 0
    REQUIRE(dmxtest::waitForChannel(pair.store, 1, 512, 99));

    DmxSnapshot snap;
    pair.store.snapshot(snap);
    CHECK(snap.channel(1, 1) == 11);
    const UniverseInfo* info = snap.info(1);
    REQUIRE(info);
    REQUIRE(info->sources.size() == 1);
    CHECK(info->sources[0].protocol == Protocol::ArtNet);
    CHECK(info->sources[0].interfaceId == 1);
    CHECK(info->sources[0].endpoint.address == kLoopbackAddress);
    CHECK(info->sources[0].endpoint.port == pair.sender.boundPort());

    CHECK(pair.sender.status().packetsOut >= 1);
    CHECK(pair.receiver.status().packetsIn >= 1);
    CHECK(pair.receiver.status().state == InterfaceState::Running);
}

TEST_CASE("artnet interface: universe offset maps port-addresses") {
    ArtNetPair pair(10);  // receiver: logical = port-address + 1 + 10
    UniverseData data{};
    data[0] = 42;
    pair.sender.send(3, data);  // sender offset 0: universe 3 -> port-address 2 -> receiver universe 13
    REQUIRE(dmxtest::waitForChannel(pair.store, 13, 1, 42));
    DmxSnapshot snap;
    pair.store.snapshot(snap);
    CHECK(snap.universe(3) == nullptr);
}

TEST_CASE("artnet interface: input can be disabled") {
    ArtNetPair pair;
    pair.receiver.setInputEnabled(false);
    UniverseData data{};
    data[0] = 1;
    pair.sender.send(1, data);
    CHECK(dmxtest::waitFor([&] { return pair.receiver.status().packetsIn >= 1; }));
    DmxSnapshot snap;
    pair.store.snapshot(snap);
    CHECK(snap.universe(1) == nullptr);
}

TEST_CASE("artnet interface: stopping releases sources and is quick") {
    ArtNetPair pair;
    UniverseData data{};
    data[0] = 5;
    pair.sender.send(1, data);
    REQUIRE(dmxtest::waitForChannel(pair.store, 1, 1, 5));

    const auto before = std::chrono::steady_clock::now();
    pair.receiver.stop();
    CHECK(std::chrono::steady_clock::now() - before < 200ms);
    CHECK(pair.receiver.state() == InterfaceState::Stopped);
    DmxSnapshot snap;
    pair.store.snapshot(snap);
    CHECK(snap.universe(1) == nullptr);

    // Restart works and picks a new port.
    std::string error;
    REQUIRE_MESSAGE(pair.receiver.start(error), error);
    CHECK(pair.receiver.running());
}

TEST_CASE("artnet interface: answers ArtPoll with an ArtPollReply") {
    UniverseStore store;
    ArtNetInterface node;
    ArtNetConfig c = loopbackConfig();
    c.announcedUniverses = {1, 2, 17};  // port-addresses 0, 1 (sub-net 0) and 16 (sub-net 1)
    c.longName = "Test visualiser";
    node.setConfig(c);
    node.attach(&store, 1);
    std::string error;
    REQUIRE_MESSAGE(node.start(error), error);

    UdpSocket console;
    REQUIRE_MESSAGE(console.open({kLoopbackAddress, 0}, false, error), error);
    std::array<std::uint8_t, artnet::kPollSize> poll{};
    REQUIRE(artnet::encodePoll({}, poll) > 0);
    REQUIRE(console.sendTo(poll, {kLoopbackAddress, node.boundPort()}));

    std::vector<artnet::PollReply> replies;
    std::array<std::uint8_t, 1024> buffer{};
    dmxtest::waitFor([&] {
        Endpoint from;
        const int n = console.receive(buffer, from, 20ms);
        if (n > 0)
            if (auto reply = artnet::decodePollReply(std::span(buffer).first(static_cast<std::size_t>(n))))
                replies.push_back(*reply);
        return replies.size() >= 2;
    });
    REQUIRE(replies.size() == 2);  // two pages: sub-net 0 and sub-net 1
    CHECK(replies[0].style == artnet::kStyleVisual);
    CHECK(replies[0].shortName == "DmxViz");
    CHECK(replies[0].longName == "Test visualiser");
    CHECK(replies[0].ip == kLoopbackAddress);
    CHECK(replies[0].numPorts == 2);
    CHECK(replies[0].portTypes[0] == 0x80);
    CHECK(replies[1].numPorts == 1);
    CHECK(replies[1].outputPortAddress(0) == 16);
    CHECK(replies[1].bindIndex == 2);
    CHECK(replies[0].nodeReport.starts_with("#0001 [0001]"));
}

TEST_CASE("artnet interface: targeted poll outside our range gets no reply") {
    UniverseStore store;
    ArtNetInterface node;
    node.setConfig(loopbackConfig());  // announces universes 1..4 = port-addresses 0..3
    node.attach(&store, 1);
    std::string error;
    REQUIRE_MESSAGE(node.start(error), error);

    UdpSocket console;
    REQUIRE_MESSAGE(console.open({kLoopbackAddress, 0}, false, error), error);
    artnet::PollPacket targeted;
    targeted.flags = 0x20;
    targeted.targetBottom = 100;
    targeted.targetTop = 200;
    std::array<std::uint8_t, artnet::kPollSize> poll{};
    artnet::encodePoll(targeted, poll);
    REQUIRE(console.sendTo(poll, {kLoopbackAddress, node.boundPort()}));
    REQUIRE(dmxtest::waitFor([&] { return node.status().packetsIn >= 1; }));

    std::array<std::uint8_t, 1024> buffer{};
    Endpoint from;
    CHECK(console.receive(buffer, from, 50ms) == 0);
}

TEST_CASE("artnet interface: malformed packets are counted, not stored") {
    UniverseStore store;
    ArtNetInterface node;
    node.setConfig(loopbackConfig());
    node.attach(&store, 1);
    std::string error;
    REQUIRE_MESSAGE(node.start(error), error);

    UdpSocket sender;
    REQUIRE_MESSAGE(sender.open({kLoopbackAddress, 0}, false, error), error);
    const std::array<std::uint8_t, 5> garbage = {1, 2, 3, 4, 5};
    REQUIRE(sender.sendTo(garbage, {kLoopbackAddress, node.boundPort()}));
    // A valid header with an impossible length field.
    std::array<std::uint8_t, artnet::kMaxDmxPacketSize> dmx{};
    const std::array<std::uint8_t, 2> two = {1, 2};
    const std::size_t size = artnet::encodeDmx({0, 0, 0, two}, dmx);
    dmx[17] = 200;
    REQUIRE(sender.sendTo(std::span(dmx).first(size), {kLoopbackAddress, node.boundPort()}));

    REQUIRE(dmxtest::waitFor([&] { return node.status().packetsInvalid >= 2; }));
    DmxSnapshot snap;
    store.snapshot(snap);
    CHECK(snap.empty());
}

TEST_CASE("artnet interface: start reports errors instead of throwing") {
    ArtNetInterface node;
    ArtNetConfig c;
    c.nic = Ipv4Address::fromOctets(203, 0, 113, 77);  // TEST-NET-3: not on this machine
    node.setConfig(c);
    std::string error;
    CHECK_FALSE(node.start(error));
    CHECK_FALSE(error.empty());
    CHECK(node.state() == InterfaceState::Error);
    CHECK(node.status().message == error);
    node.send(1, UniverseData{});  // harmless while not running
}

TEST_CASE("artnet interface: config JSON round trip") {
    ArtNetInterface a;
    ArtNetConfig c;
    c.nic = Ipv4Address::fromOctets(192, 168, 1, 10);
    c.port = 6455;
    c.universeOffset = -1;
    c.broadcastOutput = false;
    c.unicastTargets = {Endpoint{Ipv4Address::fromOctets(192, 168, 1, 20), 6454},
                        Endpoint{Ipv4Address::fromOctets(192, 168, 1, 21), 7000}};
    c.replyToPoll = false;
    c.announcedUniverses = {5, 6};
    c.shortName = "Rig A";
    a.setConfig(c);

    ArtNetInterface b;
    std::string error;
    REQUIRE_MESSAGE(b.loadConfig(a.saveConfig(), error), error);
    CHECK(b.config().nic == c.nic);
    CHECK(b.config().port == 6455);
    CHECK(b.config().universeOffset == -1);
    CHECK_FALSE(b.config().broadcastOutput);
    CHECK(b.config().unicastTargets == c.unicastTargets);
    CHECK_FALSE(b.config().replyToPoll);
    CHECK(b.config().announcedUniverses == c.announcedUniverses);
    CHECK(b.config().shortName == "Rig A");

    nlohmann::json bad = a.saveConfig();
    bad["nic"] = "not an address";
    CHECK_FALSE(b.loadConfig(bad, error));
    bad = a.saveConfig();
    bad["port"] = "6454";  // wrong type
    CHECK_FALSE(b.loadConfig(bad, error));
    CHECK(b.config().port == 6455);  // unchanged after a failed load
}
