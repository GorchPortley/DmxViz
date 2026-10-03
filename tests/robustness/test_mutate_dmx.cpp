// Mutation robustness of the network and serial DMX parsers: Art-Net, sACN, the Enttec Pro
// stream parser, and the live interfaces fed with damaged datagrams over loopback UDP.

#include "../dmx/DmxTestUtil.h"
#include "Mutate.h"

#include "dmx/UniverseStore.h"
#include "dmx/interfaces/ArtNetInterface.h"
#include "dmx/interfaces/SacnInterface.h"
#include "dmx/platform/UdpSocket.h"
#include "dmx/protocol/ArtNet.h"
#include "dmx/protocol/EnttecPro.h"
#include "dmx/protocol/Sacn.h"

#include <doctest/doctest.h>

#include <array>

using namespace dmxviz::dmx;
using namespace dmxviz::robust;

namespace {

UniverseData ramp() {
    UniverseData data;
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::uint8_t>(i * 7);
    return data;
}

// Valid Art-Net packets of every kind we handle.
std::vector<Bytes> artnetSamples() {
    std::vector<Bytes> samples;
    std::array<std::uint8_t, 1024> buffer{};
    const UniverseData data = ramp();
    for (std::size_t length : {1u, 2u, 3u, 24u, 511u, 512u}) {
        artnet::DmxPacket dmx;
        dmx.sequence = 9;
        dmx.portAddress = 0x1234 & artnet::kMaxPortAddress;
        dmx.data = std::span(data).first(length);
        const std::size_t n = artnet::encodeDmx(dmx, buffer);
        samples.emplace_back(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(n));
    }
    artnet::PollPacket poll;
    poll.flags = 0x22;
    poll.targetTop = 100;
    samples.emplace_back(buffer.begin(),
                         buffer.begin() + static_cast<std::ptrdiff_t>(artnet::encodePoll(poll, buffer)));
    artnet::PollReply base;
    base.shortName = "short";
    base.longName = "a longer name";
    const std::vector<std::uint16_t> ports{0, 1, 2, 3, 4, 300};
    for (const artnet::PollReply& page : artnet::buildPollReplies(base, ports))
        samples.emplace_back(buffer.begin(),
                             buffer.begin() + static_cast<std::ptrdiff_t>(artnet::encodePollReply(page, buffer)));
    samples.emplace_back(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(artnet::encodeSync(buffer)));
    return samples;
}

std::vector<Bytes> sacnSamples() {
    std::vector<Bytes> samples;
    std::array<std::uint8_t, sacn::kMaxDataPacketSize> buffer{};
    const UniverseData data = ramp();
    for (std::size_t length : {0u, 1u, 100u, 512u}) {
        sacn::DataPacket packet;
        packet.cid = sacn::generateCid();
        packet.sourceName = "robustness source";
        packet.universe = 1234;
        packet.sequence = 7;
        packet.slots = std::span(data).first(length);
        const std::size_t n = sacn::encodeData(packet, buffer);
        samples.emplace_back(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(n));
    }
    return samples;
}

// Everything a decoder offers, applied to one datagram.
void decodeArtnet(const Bytes& packet) {
    const auto op = artnet::opCodeOf(packet);
    const auto dmx = artnet::decodeDmx(packet);
    if (dmx) {
        CHECK(dmx->data.size() >= 1);
        CHECK(dmx->data.size() <= 512);
        CHECK(dmx->portAddress <= artnet::kMaxPortAddress);
        CHECK(op.has_value());
    }
    if (const auto poll = artnet::decodePoll(packet)) static_cast<void>(poll->targeted());
    if (const auto reply = artnet::decodePollReply(packet)) {
        CHECK(reply->numPorts <= 4);
        for (int i = 0; i < reply->numPorts; ++i) static_cast<void>(reply->outputPortAddress(i));
    }
    static_cast<void>(artnet::isSync(packet));
}

void decodeSacn(const Bytes& packet) {
    if (const auto data = sacn::decodeData(packet)) {
        CHECK(data->slots.size() <= 512);
        CHECK(data->universe >= sacn::kMinUniverse);
        CHECK(data->universe <= sacn::kMaxUniverse);
        CHECK(data->priority <= sacn::kMaxPriority);
        CHECK(data->sourceName.size() <= sacn::kSourceNameSize);
    }
    static_cast<void>(sacn::isExtendedPacket(packet));
}

// Feeds one byte stream to the Enttec parser in random chunk sizes and decodes every message.
std::size_t feedEnttec(enttec::WidgetParser& parser, const Bytes& stream, Rng& rng) {
    UniverseData frame{};
    std::size_t messages = 0;
    std::size_t offset = 0;
    while (offset < stream.size()) {
        const std::size_t chunk = std::min(stream.size() - offset, 1 + rng.below(700));
        parser.feed(std::span(stream).subspan(offset, chunk), [&](const enttec::WidgetMessage& message) {
            ++messages;
            CHECK(message.payload.size() <= enttec::kMaxPayload);
            if (const auto received = enttec::decodeReceivedDmx(message)) {
                CHECK(received->channels.size() <= enttec::kMaxPayload);
                std::copy_n(received->channels.begin(), std::min(received->channels.size(), frame.size()),
                            frame.begin());
            }
            if (const auto change = enttec::decodeChangeOfState(message)) enttec::applyChangeOfState(*change, frame);
        });
        offset += chunk;
    }
    return messages;
}

Bytes enttecStream() {
    Bytes stream;
    std::array<std::uint8_t, enttec::kMaxMessageSize> buffer{};
    auto append = [&](std::size_t n) {
        stream.insert(stream.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(n));
    };
    const UniverseData data = ramp();
    append(enttec::encodeSendDmx(data, buffer));
    append(enttec::encodeReceiveDmxOnChange(true, buffer));
    // Received DMX (label 5): status, start code, channels.
    Bytes payload{0, 0};
    payload.insert(payload.end(), data.begin(), data.end());
    append(enttec::encodeMessage(enttec::label::kReceivedDmx, payload, buffer));
    // Change of state (label 9): start index, 5 bytes of change bits, then one value per set bit.
    const Bytes change{0, 0xFF, 0x0F, 0x00, 0x80, 0x01, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
    append(enttec::encodeMessage(enttec::label::kReceivedDmxChange, change, buffer));
    append(enttec::encodeMessage(enttec::label::kGetSerialNumber, Bytes{1, 2, 3, 4}, buffer));
    return stream;
}

// Sends `packets` to the interface port, then a valid marker datagram, and waits for the marker to
// appear in the store: the interface survived and kept serving.
template <typename Make>
void floodInterface(std::uint16_t port, const std::vector<Bytes>& packets, UniverseStore& store,
                    UniverseId markerUniverse, Make&& makeMarker) {
    UdpSocket socket;
    std::string error;
    REQUIRE_MESSAGE(socket.open(Endpoint{kLoopbackAddress, 0}, false, error), error);
    const Endpoint target{kLoopbackAddress, port};
    for (const Bytes& packet : packets) socket.sendTo(packet, target);
    const Bytes marker = makeMarker();
    // UDP may drop under load; keep sending the marker until it shows up.
    DmxSnapshot snapshot;
    const bool arrived = dmxtest::waitFor(
        [&] {
            socket.sendTo(marker, target);
            store.snapshot(snapshot);
            return snapshot.channel(markerUniverse, 1) == 77;
        },
        std::chrono::milliseconds(3000));
    CHECK(arrived);
}

}  // namespace

TEST_CASE("mutation: Art-Net decoders survive damaged packets") {
    Rng rng(0xA27E7001);
    const std::vector<Bytes> samples = artnetSamples();
    REQUIRE(samples.size() > 6);
    for (const Bytes& sample : samples) decodeArtnet(sample);  // the valid ones first
    SlowestCall slowest;
    for (std::size_t i = 0; i < 40000; ++i) {
        const Bytes mutated = mutateBytes(samples[i % samples.size()], rng, i);
        slowest.run([&] { decodeArtnet(mutated); });
    }
    CHECK(slowest.seconds() < 1.0);
}

TEST_CASE("mutation: sACN decoder survives damaged packets") {
    Rng rng(0x5ACE0002);
    const std::vector<Bytes> samples = sacnSamples();
    REQUIRE(samples.size() == 4);
    for (const Bytes& sample : samples) {
        REQUIRE(sacn::decodeData(sample).has_value());
        decodeSacn(sample);
    }
    SlowestCall slowest;
    for (std::size_t i = 0; i < 40000; ++i) {
        const Bytes mutated = mutateBytes(samples[i % samples.size()], rng, i);
        slowest.run([&] { decodeSacn(mutated); });
    }
    CHECK(slowest.seconds() < 1.0);

    // CID text parsing (project files, settings).
    const std::string cid = sacn::cidToString(sacn::generateCid());
    REQUIRE(sacn::parseCid(cid).has_value());
    for (std::size_t i = 0; i < 5000; ++i) {
        const Bytes mutated = mutateBytes(toBytes(cid), rng, i);
        static_cast<void>(sacn::parseCid(toText(mutated)));
    }
}

TEST_CASE("mutation: Enttec Pro stream parser resyncs on damaged streams") {
    Rng rng(0xE177EC03);
    const Bytes stream = enttecStream();
    enttec::WidgetParser clean;
    feedEnttec(clean, stream, rng);
    CHECK(clean.framingErrors() == 0);

    enttec::WidgetParser parser;  // reused: state must survive any garbage
    for (std::size_t i = 0; i < 4000; ++i) feedEnttec(parser, mutateBytes(stream, rng, i), rng);
    // After any garbage a good stream is parsed completely again (the first copy may be eaten by
    // a half-received bogus message).
    feedEnttec(parser, stream, rng);
    CHECK(feedEnttec(parser, stream, rng) == 5);
    parser.reset();
    CHECK(feedEnttec(parser, stream, rng) == 5);

    // A message that claims the maximum length but is cut short must not read past what was fed.
    Bytes truncated{enttec::kStartOfMessage, enttec::label::kReceivedDmx, 0xFF, 0xFF, 0x00};
    feedEnttec(parser, truncated, rng);
    // Absurd lengths are bad framing, not an allocation.
    for (int i = 0; i < 1000; ++i)
        feedEnttec(parser,
                   Bytes{enttec::kStartOfMessage, 5, static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(i >> 2)},
                   rng);
}

TEST_CASE("mutation: Art-Net interface keeps serving after damaged datagrams") {
    Rng rng(0xA27E7004);
    UniverseStore store;
    ArtNetInterface iface;
    ArtNetConfig config;
    config.nic = kLoopbackAddress;
    config.port = 0;
    config.broadcastOutput = false;
    iface.setConfig(config);
    iface.attach(&store, 1);
    std::string error;
    REQUIRE_MESSAGE(iface.start(error), error);

    const std::vector<Bytes> samples = artnetSamples();
    std::vector<Bytes> packets;
    for (std::size_t i = 0; i < 1500; ++i) packets.push_back(mutateBytes(samples[i % samples.size()], rng, i));
    floodInterface(iface.boundPort(), packets, store, 200, [] {
        std::array<std::uint8_t, 600> buffer{};
        artnet::DmxPacket marker;
        marker.portAddress = 199;  // universe 200
        const std::array<std::uint8_t, 4> values{77, 0, 0, 0};
        marker.data = values;
        const std::size_t n = artnet::encodeDmx(marker, buffer);
        return Bytes(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(n));
    });
    iface.stop();
}

TEST_CASE("mutation: sACN interface keeps serving after damaged datagrams") {
    Rng rng(0x5ACE0005);
    UniverseStore store;
    SacnInterface iface;
    SacnConfig config;
    config.nic = kLoopbackAddress;
    config.port = 0;
    config.multicastInput = false;
    config.acceptAllUniverses = true;
    iface.setConfig(config);
    iface.attach(&store, 1);
    std::string error;
    REQUIRE_MESSAGE(iface.start(error), error);

    const std::vector<Bytes> samples = sacnSamples();
    std::vector<Bytes> packets;
    for (std::size_t i = 0; i < 1500; ++i) packets.push_back(mutateBytes(samples[i % samples.size()], rng, i));
    const sacn::Cid cid = sacn::generateCid();
    floodInterface(iface.boundPort(), packets, store, 300, [&] {
        std::array<std::uint8_t, sacn::kMaxDataPacketSize> buffer{};
        sacn::DataPacket marker;
        marker.cid = cid;
        marker.universe = 300;
        const std::array<std::uint8_t, 2> values{77, 0};
        marker.slots = values;
        marker.sequence = 200;  // any value: the first packet of a new CID is always accepted
        const std::size_t n = sacn::encodeData(marker, buffer);
        return Bytes(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(n));
    });
    iface.stop();
}
