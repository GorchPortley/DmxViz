// sACN (E1.31-2018) codec: golden layout from the standard's table 4-1, round trips,
// malformed packets and the sequence-number rule.

#include "dmx/protocol/Sacn.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <vector>

using namespace dmxviz::dmx;

namespace {

constexpr sacn::Cid kTestCid = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

std::vector<std::uint8_t> encode(const sacn::DataPacket& packet) {
    std::array<std::uint8_t, sacn::kMaxDataPacketSize> buffer{};
    const std::size_t n = sacn::encodeData(packet, buffer);
    return {buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(n)};
}

std::vector<std::uint8_t> goldenFourSlotPacket() {
    std::vector<std::uint8_t> p = {
        // Root layer
        0x00, 0x10, 0x00, 0x00,                                                  // preamble, post-amble
        0x41, 0x53, 0x43, 0x2D, 0x45, 0x31, 0x2E, 0x31, 0x37, 0x00, 0x00, 0x00,  // "ASC-E1.17"
        0x70, 0x72,                                                              // flags | 114
        0x00, 0x00, 0x00, 0x04,                                                  // VECTOR_ROOT_E131_DATA
    };
    p.insert(p.end(), kTestCid.begin(), kTestCid.end());
    // Framing layer
    p.insert(p.end(), {0x70, 0x5C, 0x00, 0x00, 0x00, 0x02});  // flags | 92, VECTOR_E131_DATA_PACKET
    std::array<std::uint8_t, 64> name{};
    name[0] = 'T';
    name[1] = 'e';
    name[2] = 's';
    name[3] = 't';
    p.insert(p.end(), name.begin(), name.end());
    p.insert(p.end(), {150,           // priority
                       0x00, 0x00,    // sync address
                       0x05,          // sequence
                       0x80,          // options: preview
                       0x01, 0x02});  // universe 258
    // DMP layer
    p.insert(p.end(), {0x70, 0x0F,  // flags | 15
                       0x02, 0xA1,  // VECTOR_DMP_SET_PROPERTY, address + data type
                       0x00, 0x00,  // first property address
                       0x00, 0x01,  // address increment
                       0x00, 0x05,  // property value count: start code + 4
                       0x00,        // start code
                       10, 20, 30, 40});
    return p;
}

}  // namespace

TEST_CASE("sacn: data packet golden layout") {
    const std::array<std::uint8_t, 4> slots = {10, 20, 30, 40};
    sacn::DataPacket packet;
    packet.cid = kTestCid;
    packet.sourceName = "Test";
    packet.priority = 150;
    packet.sequence = 5;
    packet.preview = true;
    packet.universe = 258;
    packet.slots = slots;

    const auto bytes = encode(packet);
    const auto golden = goldenFourSlotPacket();
    REQUIRE(bytes.size() == 130);
    CHECK(bytes == golden);
}

TEST_CASE("sacn: decode golden packet") {
    const auto golden = goldenFourSlotPacket();
    const auto packet = sacn::decodeData(golden);
    REQUIRE(packet);
    CHECK(packet->cid == kTestCid);
    CHECK(packet->sourceName == "Test");
    CHECK(packet->priority == 150);
    CHECK(packet->sequence == 5);
    CHECK(packet->preview);
    CHECK_FALSE(packet->streamTerminated);
    CHECK(packet->universe == 258);
    CHECK(packet->startCode == 0);
    REQUIRE(packet->slots.size() == 4);
    CHECK(packet->slots[3] == 40);
}

TEST_CASE("sacn: full universe round trip with options") {
    std::array<std::uint8_t, 512> slots{};
    for (std::size_t i = 0; i < slots.size(); ++i) slots[i] = static_cast<std::uint8_t>(255 - i % 256);
    sacn::DataPacket packet;
    packet.cid = sacn::generateCid();
    packet.sourceName = std::string_view("A name that is longer than sixty-four characters, so it has to be cut.");
    packet.priority = 200;
    packet.syncAddress = 7;
    packet.sequence = 255;
    packet.streamTerminated = true;
    packet.forceSync = true;
    packet.universe = sacn::kMaxUniverse;
    packet.slots = slots;

    const auto bytes = encode(packet);
    REQUIRE(bytes.size() == sacn::kMaxDataPacketSize);
    const auto decoded = sacn::decodeData(bytes);
    REQUIRE(decoded);
    CHECK(decoded->cid == packet.cid);
    CHECK(decoded->sourceName.size() == 63);
    CHECK(decoded->priority == 200);
    CHECK(decoded->syncAddress == 7);
    CHECK(decoded->sequence == 255);
    CHECK(decoded->streamTerminated);
    CHECK(decoded->forceSync);
    CHECK_FALSE(decoded->preview);
    CHECK(decoded->universe == sacn::kMaxUniverse);
    REQUIRE(decoded->slots.size() == 512);
    CHECK(std::equal(slots.begin(), slots.end(), decoded->slots.begin()));
}

TEST_CASE("sacn: encode rejects invalid universes") {
    std::array<std::uint8_t, sacn::kMaxDataPacketSize> buffer{};
    sacn::DataPacket packet;
    packet.universe = 0;
    CHECK(sacn::encodeData(packet, buffer) == 0);
    packet.universe = 64000;
    CHECK(sacn::encodeData(packet, buffer) == 0);
    packet.universe = 1;
    CHECK(sacn::encodeData(packet, buffer) == sacn::kDataHeaderSize);  // start code only
}

TEST_CASE("sacn: malformed packets are rejected") {
    const auto good = goldenFourSlotPacket();
    REQUIRE(sacn::decodeData(good));

    auto expectReject = [&](std::size_t offset, std::uint8_t value) {
        auto bad = good;
        bad[offset] = value;
        CHECK_FALSE(sacn::decodeData(bad));
    };
    SUBCASE("preamble") {
        expectReject(1, 0x11);
    }
    SUBCASE("ACN identifier") {
        expectReject(4, 'X');
    }
    SUBCASE("root flags") {
        expectReject(16, 0x60);
    }
    SUBCASE("root vector") {
        expectReject(21, 0x03);
    }
    SUBCASE("framing length") {
        expectReject(39, 0x5B);
    }
    SUBCASE("framing vector") {
        expectReject(43, 0x01);
    }
    SUBCASE("DMP vector") {
        expectReject(117, 0x01);
    }
    SUBCASE("DMP address type") {
        expectReject(118, 0xA2);
    }
    SUBCASE("address increment") {
        expectReject(122, 0x02);
    }
    SUBCASE("property count") {
        expectReject(124, 0x06);
    }
    SUBCASE("universe 0") {
        auto bad = good;
        bad[113] = 0;
        bad[114] = 0;
        CHECK_FALSE(sacn::decodeData(bad));
    }
    SUBCASE("truncated") {
        const std::vector<std::uint8_t> bad(good.begin(), good.end() - 1);
        CHECK_FALSE(sacn::decodeData(bad));
    }
    SUBCASE("too short for a header") {
        const std::vector<std::uint8_t> bad(good.begin(), good.begin() + 100);
        CHECK_FALSE(sacn::decodeData(bad));
    }
    SUBCASE("extended (sync) packets are not data") {
        auto sync = good;
        sync[21] = 0x08;
        CHECK_FALSE(sacn::decodeData(sync));
        CHECK(sacn::isExtendedPacket(sync));
        CHECK_FALSE(sacn::isExtendedPacket(good));
    }
}

TEST_CASE("sacn: priority above 200 is clamped, non-zero start code is reported") {
    auto bytes = goldenFourSlotPacket();
    bytes[108] = 250;
    bytes[125] = 0xDD;  // per-address priority start code
    const auto packet = sacn::decodeData(bytes);
    REQUIRE(packet);
    CHECK(packet->priority == 200);
    CHECK(packet->startCode == 0xDD);
}

TEST_CASE("sacn: sequence numbers (E1.31 6.7.2)") {
    CHECK(sacn::isSequenceAcceptable(10, 11));
    CHECK_FALSE(sacn::isSequenceAcceptable(10, 10));   // duplicate
    CHECK_FALSE(sacn::isSequenceAcceptable(10, 9));    // late
    CHECK_FALSE(sacn::isSequenceAcceptable(10, 247));  // -19 after wrap
    CHECK(sacn::isSequenceAcceptable(10, 246));        // -20: treated as a restart
    CHECK(sacn::isSequenceAcceptable(255, 0));         // wrap-around
    CHECK(sacn::isSequenceAcceptable(250, 3));
    CHECK(sacn::isSequenceAcceptable(0, 128));  // large jumps forward are accepted
}

TEST_CASE("sacn: multicast address") {
    CHECK(sacn::multicastAddress(1) == Ipv4Address::fromOctets(239, 255, 0, 1));
    CHECK(sacn::multicastAddress(258) == Ipv4Address::fromOctets(239, 255, 1, 2));
    CHECK(sacn::multicastAddress(63999).toString() == "239.255.249.255");
}

TEST_CASE("sacn: CID generation and text form") {
    const sacn::Cid a = sacn::generateCid();
    const sacn::Cid b = sacn::generateCid();
    CHECK(a != b);
    CHECK((a[6] & 0xF0) == 0x40);  // version 4
    CHECK((a[8] & 0xC0) == 0x80);  // RFC 4122 variant

    const std::string text = sacn::cidToString(kTestCid);
    CHECK(text == "01020304-0506-0708-090a-0b0c0d0e0f10");
    CHECK(sacn::parseCid(text) == kTestCid);
    CHECK(sacn::parseCid("01020304-0506-0708-090A-0B0C0D0E0F10") == kTestCid);
    CHECK_FALSE(sacn::parseCid("01020304-0506-0708-090a-0b0c0d0e0f1"));
    CHECK_FALSE(sacn::parseCid("01020304x0506-0708-090a-0b0c0d0e0f10"));
    CHECK_FALSE(sacn::parseCid("0102030g-0506-0708-090a-0b0c0d0e0f10"));
}
