// Art-Net codec: golden byte layouts from the Art-Net 4 specification, round trips and
// rejection of malformed packets.

#include "dmx/protocol/ArtNet.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <vector>

using namespace dmxviz::dmx;

namespace {

std::vector<std::uint8_t> encodeDmxPacket(const artnet::DmxPacket& packet) {
    std::array<std::uint8_t, artnet::kMaxDmxPacketSize> buffer{};
    const std::size_t n = artnet::encodeDmx(packet, buffer);
    return {buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(n)};
}

}  // namespace

TEST_CASE("artnet: ArtDmx golden layout") {
    const std::array<std::uint8_t, 3> data = {1, 2, 3};
    artnet::DmxPacket packet;
    packet.sequence = 0x2A;
    packet.physical = 1;
    packet.portAddress = artnet::makePortAddress(0x12, 3, 4);  // Net 0x12, Sub-Net 3, Universe 4
    packet.data = data;

    const std::vector<std::uint8_t> expected = {
        'A',  'r',  't', '-', 'N', 'e', 't', 0,  // ID
        0x00, 0x50,                              // OpDmx, little endian
        0x00, 0x0E,                              // protocol version 14
        0x2A,                                    // sequence
        0x01,                                    // physical
        0x34,                                    // SubUni: Sub-Net 3, Universe 4
        0x12,                                    // Net
        0x00, 0x04,                              // length, big endian, padded to even
        1,    2,    3,   0,                      // data + pad
    };
    CHECK(encodeDmxPacket(packet) == expected);
}

TEST_CASE("artnet: ArtDmx round trip") {
    std::array<std::uint8_t, 512> data{};
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::uint8_t>(i * 7);
    artnet::DmxPacket packet;
    packet.sequence = 200;
    packet.portAddress = artnet::kMaxPortAddress;
    packet.data = data;
    const auto bytes = encodeDmxPacket(packet);
    REQUIRE(bytes.size() == artnet::kMaxDmxPacketSize);

    const auto decoded = artnet::decodeDmx(bytes);
    REQUIRE(decoded);
    CHECK(decoded->sequence == 200);
    CHECK(decoded->portAddress == 0x7FFF);
    CHECK(artnet::netOf(decoded->portAddress) == 0x7F);
    CHECK(artnet::subNetOf(decoded->portAddress) == 0xF);
    CHECK(artnet::universeOf(decoded->portAddress) == 0xF);
    REQUIRE(decoded->data.size() == 512);
    CHECK(std::equal(data.begin(), data.end(), decoded->data.begin()));
}

TEST_CASE("artnet: ArtDmx encode rejects bad input") {
    std::array<std::uint8_t, artnet::kMaxDmxPacketSize> buffer{};
    std::array<std::uint8_t, 513> tooMany{};
    artnet::DmxPacket packet;
    CHECK(artnet::encodeDmx(packet, buffer) == 0);  // no data
    packet.data = tooMany;
    CHECK(artnet::encodeDmx(packet, buffer) == 0);
    packet.data = std::span<const std::uint8_t>(tooMany).first(10);
    packet.portAddress = 0x8000;
    CHECK(artnet::encodeDmx(packet, buffer) == 0);
    packet.portAddress = 0;
    CHECK(artnet::encodeDmx(packet, std::span<std::uint8_t>(buffer).first(20)) == 0);  // buffer too small
    CHECK(artnet::encodeDmx(packet, buffer) == 28);
}

TEST_CASE("artnet: malformed ArtDmx is rejected") {
    const std::array<std::uint8_t, 4> data = {10, 20, 30, 40};
    artnet::DmxPacket packet;
    packet.data = data;
    const auto good = encodeDmxPacket(packet);
    REQUIRE(artnet::decodeDmx(good));

    SUBCASE("wrong ID") {
        auto bad = good;
        bad[0] = 'X';
        CHECK_FALSE(artnet::decodeDmx(bad));
        CHECK_FALSE(artnet::opCodeOf(bad));
    }
    SUBCASE("wrong OpCode") {
        auto bad = good;
        bad[9] = 0x20;  // OpPoll
        CHECK_FALSE(artnet::decodeDmx(bad));
    }
    SUBCASE("old protocol version") {
        auto bad = good;
        bad[11] = 13;
        CHECK_FALSE(artnet::decodeDmx(bad));
    }
    SUBCASE("length larger than packet") {
        auto bad = good;
        bad[17] = 6;
        CHECK_FALSE(artnet::decodeDmx(bad));
    }
    SUBCASE("zero length") {
        auto bad = good;
        bad[17] = 0;
        CHECK_FALSE(artnet::decodeDmx(bad));
    }
    SUBCASE("length above 512") {
        std::vector<std::uint8_t> bad(18 + 514, 0);
        std::copy(good.begin(), good.begin() + 18, bad.begin());
        bad[16] = 0x02;
        bad[17] = 0x02;  // 514
        CHECK_FALSE(artnet::decodeDmx(bad));
    }
    SUBCASE("truncated header") {
        const std::vector<std::uint8_t> bad(good.begin(), good.begin() + 17);
        CHECK_FALSE(artnet::decodeDmx(bad));
    }
    SUBCASE("odd length is tolerated") {
        auto odd = good;
        odd[17] = 3;
        const auto decoded = artnet::decodeDmx(odd);
        REQUIRE(decoded);
        CHECK(decoded->data.size() == 3);
    }
}

TEST_CASE("artnet: ArtPoll encode/decode") {
    artnet::PollPacket poll;
    poll.flags = 0x22;  // targeted + reply on change
    poll.targetBottom = 0x0010;
    poll.targetTop = 0x001F;
    std::array<std::uint8_t, artnet::kPollSize> buffer{};
    REQUIRE(artnet::encodePoll(poll, buffer) == artnet::kPollSize);
    CHECK(buffer[8] == 0x00);
    CHECK(buffer[9] == 0x20);
    CHECK(buffer[12] == 0x22);
    CHECK(buffer[14] == 0x00);
    CHECK(buffer[15] == 0x1F);  // target top, big endian
    CHECK(buffer[17] == 0x10);  // target bottom

    const auto decoded = artnet::decodePoll(buffer);
    REQUIRE(decoded);
    CHECK(decoded->targeted());
    CHECK(decoded->targetTop == 0x1F);
    CHECK(decoded->targetBottom == 0x10);

    // A 14-byte Art-Net 3 poll has no target fields and is never targeted.
    const auto short14 = artnet::decodePoll(std::span<const std::uint8_t>(buffer).first(14));
    REQUIRE(short14);
    CHECK_FALSE(short14->targeted());

    auto bad = buffer;
    bad[11] = 10;  // protocol version 10
    CHECK_FALSE(artnet::decodePoll(bad));
    CHECK_FALSE(artnet::decodePoll(std::span<const std::uint8_t>(buffer).first(11)));
}

TEST_CASE("artnet: ArtPollReply golden layout") {
    artnet::PollReply base;
    base.ip = Ipv4Address::fromOctets(192, 168, 1, 20);
    base.mac = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    base.bindIp = base.ip;
    base.shortName = "DmxViz";
    base.longName = "DmxViz visualiser";
    artnet::formatNodeReport(base.nodeReport, 0x0001, 7, "OK");
    CHECK(base.nodeReport == "#0001 [0007] OK");

    const std::array<std::uint16_t, 4> ports = {0x0100, 0x0101, 0x0102, 0x0103};
    const auto pages = artnet::buildPollReplies(base, ports);
    REQUIRE(pages.size() == 1);

    std::array<std::uint8_t, artnet::kPollReplySize> b{};
    REQUIRE(artnet::encodePollReply(pages[0], b) == 239);
    CHECK(std::string(reinterpret_cast<const char*>(b.data())) == "Art-Net");
    CHECK(b[8] == 0x00);
    CHECK(b[9] == 0x21);  // OpPollReply
    CHECK(b[10] == 192);
    CHECK(b[11] == 168);
    CHECK(b[12] == 1);
    CHECK(b[13] == 20);
    CHECK(b[14] == 0x36);  // port 0x1936, low byte first
    CHECK(b[15] == 0x19);
    CHECK(b[18] == 0x01);  // NetSwitch
    CHECK(b[19] == 0x00);  // SubSwitch
    CHECK(b[20] == 0x00);  // OEM hi
    CHECK(b[21] == 0xFF);  // OEM lo (OemUnknown)
    CHECK(std::string(reinterpret_cast<const char*>(&b[26])) == "DmxViz");
    CHECK(std::string(reinterpret_cast<const char*>(&b[44])) == "DmxViz visualiser");
    CHECK(std::string(reinterpret_cast<const char*>(&b[108])) == "#0001 [0007] OK");
    CHECK(b[172] == 0);
    CHECK(b[173] == 4);  // NumPorts
    for (int i = 0; i < 4; ++i) {
        CHECK(b[174 + i] == 0x80);  // PortTypes: output from Art-Net, DMX512
        CHECK(b[190 + i] == i);     // SwOut
    }
    CHECK(b[200] == 0x06);  // Style: StVisual
    CHECK(b[201] == 0x02);  // MAC
    CHECK(b[206] == 0x55);
    CHECK(b[207] == 192);   // BindIp
    CHECK(b[211] == 1);     // BindIndex
    CHECK(b[212] == 0x08);  // Status2: 15-bit port-addresses

    const auto decoded = artnet::decodePollReply(b);
    REQUIRE(decoded);
    CHECK(decoded->ip == base.ip);
    CHECK(decoded->udpPort == 6454);
    CHECK(decoded->shortName == "DmxViz");
    CHECK(decoded->longName == "DmxViz visualiser");
    CHECK(decoded->numPorts == 4);
    CHECK(decoded->outputPortAddress(3) == 0x0103);
    CHECK(decoded->style == artnet::kStyleVisual);
    CHECK(decoded->mac == base.mac);
}

TEST_CASE("artnet: long names are truncated with a terminating NUL") {
    artnet::PollReply reply;
    reply.shortName = std::string(40, 'S');
    reply.longName = std::string(100, 'L');
    std::array<std::uint8_t, artnet::kPollReplySize> b{};
    REQUIRE(artnet::encodePollReply(reply, b) == 239);
    const auto decoded = artnet::decodePollReply(b);
    REQUIRE(decoded);
    CHECK(decoded->shortName.size() == 17);
    CHECK(decoded->longName.size() == 63);
}

TEST_CASE("artnet: poll reply pages group ports by Net/Sub-Net") {
    artnet::PollReply base;
    // Five ports in sub-net 0, one in sub-net 1, one in another net; duplicates removed.
    const std::vector<std::uint16_t> ports = {0, 1, 2, 3, 4, 4, 0x10, 0x0200};
    const auto pages = artnet::buildPollReplies(base, ports);
    REQUIRE(pages.size() == 4);
    CHECK(pages[0].numPorts == 4);
    CHECK(pages[0].bindIndex == 1);
    CHECK(pages[1].numPorts == 1);
    CHECK(pages[1].outputPortAddress(0) == 4);
    CHECK(pages[2].outputPortAddress(0) == 0x10);
    CHECK(pages[3].netSwitch == 2);
    CHECK(pages[3].bindIndex == 4);
}

TEST_CASE("artnet: ArtSync is recognised") {
    std::array<std::uint8_t, artnet::kSyncSize> b{};
    REQUIRE(artnet::encodeSync(b) == 14);
    CHECK(artnet::isSync(b));
    CHECK(artnet::opCodeOf(b) == 0x5200);
    CHECK_FALSE(artnet::decodeDmx(b));
    CHECK_FALSE(artnet::isSync(std::span<const std::uint8_t>(b).first(13)));
}
