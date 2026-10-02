// Enttec DMX USB Pro widget protocol: message framing, label 5/9 decoding and the
// incremental parser's handling of split reads, garbage and broken messages.

#include "dmx/protocol/EnttecPro.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <vector>

using namespace dmxviz::dmx;

namespace {

struct Captured {
    std::uint8_t label;
    std::vector<std::uint8_t> payload;
};

std::vector<Captured> feedAll(enttec::WidgetParser& parser, const std::vector<std::uint8_t>& bytes,
                              std::size_t chunkSize) {
    std::vector<Captured> out;
    for (std::size_t i = 0; i < bytes.size(); i += chunkSize) {
        const std::size_t n = std::min(chunkSize, bytes.size() - i);
        parser.feed(std::span<const std::uint8_t>(bytes.data() + i, n), [&](const enttec::WidgetMessage& m) {
            out.push_back({m.label, {m.payload.begin(), m.payload.end()}});
        });
    }
    return out;
}

std::vector<std::uint8_t> frame(std::uint8_t label, const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> out(payload.size() + enttec::kFrameOverhead);
    REQUIRE(enttec::encodeMessage(label, payload, out) == out.size());
    return out;
}

}  // namespace

TEST_CASE("enttec: send DMX (label 6) golden layout") {
    const std::array<std::uint8_t, 3> channels = {1, 2, 255};
    std::array<std::uint8_t, enttec::kMaxMessageSize> b{};
    const std::size_t n = enttec::encodeSendDmx(channels, b);
    // Short frames are padded to 24 channels: payload = start code + 24.
    REQUIRE(n == 30);
    CHECK(b[0] == 0x7E);
    CHECK(b[1] == 6);
    CHECK(b[2] == 25);  // length LSB
    CHECK(b[3] == 0);   // length MSB
    CHECK(b[4] == 0);   // start code
    CHECK(b[5] == 1);
    CHECK(b[6] == 2);
    CHECK(b[7] == 255);
    CHECK(b[8] == 0);
    CHECK(b[28] == 0);
    CHECK(b[29] == 0xE7);

    std::array<std::uint8_t, 512> full{};
    full[511] = 42;
    REQUIRE(enttec::encodeSendDmx(full, b) == 518);
    CHECK(b[2] == 0x01);  // 513 = 0x0201
    CHECK(b[3] == 0x02);
    CHECK(b[516] == 42);
    CHECK(b[517] == 0xE7);
}

TEST_CASE("enttec: receive-on-change (label 8)") {
    std::array<std::uint8_t, 8> b{};
    REQUIRE(enttec::encodeReceiveDmxOnChange(false, b) == 6);
    const std::array<std::uint8_t, 6> expected = {0x7E, 8, 1, 0, 0, 0xE7};
    CHECK(std::equal(expected.begin(), expected.end(), b.begin()));
    REQUIRE(enttec::encodeReceiveDmxOnChange(true, b) == 6);
    CHECK(b[4] == 1);
}

TEST_CASE("enttec: encodeMessage limits") {
    std::vector<std::uint8_t> big(601);
    std::vector<std::uint8_t> out(700);
    CHECK(enttec::encodeMessage(5, big, out) == 0);
    big.resize(600);
    CHECK(enttec::encodeMessage(5, big, out) == 605);
    CHECK(enttec::encodeMessage(5, big, std::span<std::uint8_t>(out).first(604)) == 0);
}

TEST_CASE("enttec: decode received DMX (label 5)") {
    enttec::WidgetParser parser;
    const auto bytes = frame(5, {0x00, 0x00, 11, 22, 33});
    const auto messages = feedAll(parser, bytes, bytes.size());
    REQUIRE(messages.size() == 1);
    const enttec::WidgetMessage m{messages[0].label, messages[0].payload};
    const auto dmx = enttec::decodeReceivedDmx(m);
    REQUIRE(dmx);
    CHECK(dmx->valid());
    CHECK(dmx->startCode == 0);
    REQUIRE(dmx->channels.size() == 3);
    CHECK(dmx->channels[2] == 33);

    const std::vector<std::uint8_t> overflow = {0x01, 0x00, 1};
    const auto bad = enttec::decodeReceivedDmx({5, overflow});
    REQUIRE(bad);
    CHECK_FALSE(bad->valid());
    CHECK_FALSE(enttec::decodeReceivedDmx({6, overflow}));  // wrong label
    const std::vector<std::uint8_t> tooShort = {0x00};
    CHECK_FALSE(enttec::decodeReceivedDmx({5, tooShort}));
}

TEST_CASE("enttec: change of state (label 9)") {
    UniverseData frameData{};
    frameData[0] = 9;

    SUBCASE("block starting at the start code") {
        // Bits 1 and 3 set: byte 1 (channel 1) and byte 3 (channel 3).
        const std::vector<std::uint8_t> payload = {0, 0b00001010, 0, 0, 0, 0, 100, 200};
        const auto change = enttec::decodeChangeOfState({9, payload});
        REQUIRE(change);
        CHECK(change->firstByteIndex == 0);
        REQUIRE(enttec::applyChangeOfState(*change, frameData));
        CHECK(frameData[0] == 100);
        CHECK(frameData[1] == 0);
        CHECK(frameData[2] == 200);
    }
    SUBCASE("later block") {
        // Block 2 covers bytes 16..55; bit 0 -> byte 16 (channel 16), bit 39 -> channel 55.
        const std::vector<std::uint8_t> payload = {2, 0x01, 0, 0, 0, 0x80, 7, 8};
        const auto change = enttec::decodeChangeOfState({9, payload});
        REQUIRE(change);
        REQUIRE(enttec::applyChangeOfState(*change, frameData));
        CHECK(frameData[15] == 7);
        CHECK(frameData[54] == 8);
    }
    SUBCASE("zero start code reported as changed is fine") {
        const std::vector<std::uint8_t> payload = {0, 0b00000011, 0, 0, 0, 0, 0, 50};
        const auto change = enttec::decodeChangeOfState({9, payload});
        REQUIRE(change);
        REQUIRE(enttec::applyChangeOfState(*change, frameData));
        CHECK(frameData[0] == 50);
    }
    SUBCASE("non-zero start code is ignored") {
        const std::vector<std::uint8_t> payload = {0, 0b00000011, 0, 0, 0, 0, 0xCC, 50};
        const auto change = enttec::decodeChangeOfState({9, payload});
        REQUIRE(change);
        CHECK_FALSE(enttec::applyChangeOfState(*change, frameData));
        CHECK(frameData[0] == 9);  // untouched
    }
    SUBCASE("value count must match the bit count") {
        const std::vector<std::uint8_t> payload = {0, 0b00000110, 0, 0, 0, 0, 1};
        CHECK_FALSE(enttec::decodeChangeOfState({9, payload}));
    }
    SUBCASE("beyond channel 512") {
        const std::vector<std::uint8_t> payload = {64, 0x02, 0, 0, 0, 0, 1};  // byte 513
        const auto change = enttec::decodeChangeOfState({9, payload});
        REQUIRE(change);
        CHECK_FALSE(enttec::applyChangeOfState(*change, frameData));
    }
}

TEST_CASE("enttec: parser handles split reads") {
    std::vector<std::uint8_t> stream = frame(5, {0, 0, 1, 2, 3});
    const auto second = frame(9, {0, 0x02, 0, 0, 0, 0, 77});
    stream.insert(stream.end(), second.begin(), second.end());

    for (std::size_t chunk : {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{7}, stream.size()}) {
        CAPTURE(chunk);
        enttec::WidgetParser parser;
        const auto messages = feedAll(parser, stream, chunk);
        REQUIRE(messages.size() == 2);
        CHECK(messages[0].label == 5);
        CHECK(messages[0].payload == std::vector<std::uint8_t>{0, 0, 1, 2, 3});
        CHECK(messages[1].label == 9);
        CHECK(messages[1].payload.back() == 77);
        CHECK(parser.framingErrors() == 0);
    }
}

TEST_CASE("enttec: parser skips garbage and resyncs") {
    const auto good = frame(5, {0, 0, 42});

    SUBCASE("leading garbage without start bytes") {
        std::vector<std::uint8_t> stream = {0x01, 0x02, 0xE7, 0xFF};
        stream.insert(stream.end(), good.begin(), good.end());
        enttec::WidgetParser parser;
        const auto messages = feedAll(parser, stream, 3);
        REQUIRE(messages.size() == 1);
        CHECK(messages[0].payload.back() == 42);
        CHECK(parser.discardedBytes() == 4);
    }
    SUBCASE("false start byte that swallows a real message") {
        // 0x7E 0x05 0x20 0x00 claims a 32-byte payload; the real message lies inside
        // that span. The missing end byte must make the parser rescan and find it.
        std::vector<std::uint8_t> stream = {0x7E, 0x05, 0x20, 0x00, 0x11};
        stream.insert(stream.end(), good.begin(), good.end());
        stream.resize(stream.size() + 40, 0x00);  // more noise so the bogus frame completes
        const auto tail = frame(5, {0, 0, 43});
        stream.insert(stream.end(), tail.begin(), tail.end());
        for (std::size_t chunk : {std::size_t{1}, std::size_t{5}, stream.size()}) {
            CAPTURE(chunk);
            enttec::WidgetParser parser;
            const auto messages = feedAll(parser, stream, chunk);
            REQUIRE(messages.size() == 2);
            CHECK(messages[0].payload.back() == 42);
            CHECK(messages[1].payload.back() == 43);
            CHECK(parser.framingErrors() >= 1);
        }
    }
    SUBCASE("oversized length field") {
        std::vector<std::uint8_t> stream = {0x7E, 0x05, 0xFF, 0x7F};
        stream.insert(stream.end(), good.begin(), good.end());
        enttec::WidgetParser parser;
        const auto messages = feedAll(parser, stream, 1);
        REQUIRE(messages.size() == 1);
        CHECK(parser.framingErrors() == 1);
    }
    SUBCASE("message cut off by a new one") {
        std::vector<std::uint8_t> stream(good.begin(), good.begin() + 5);  // truncated
        stream.insert(stream.end(), good.begin(), good.end());
        enttec::WidgetParser parser;
        const auto messages = feedAll(parser, stream, 2);
        REQUIRE(messages.size() == 1);
        CHECK(messages[0].payload == std::vector<std::uint8_t>{0, 0, 42});
    }
    SUBCASE("maximum size message") {
        std::vector<std::uint8_t> payload(enttec::kMaxPayload, 0x7E);  // start bytes inside the payload
        const auto big = frame(5, payload);
        std::vector<std::uint8_t> stream = big;
        stream.insert(stream.end(), good.begin(), good.end());
        enttec::WidgetParser parser;
        const auto messages = feedAll(parser, stream, 64);
        REQUIRE(messages.size() == 2);
        CHECK(messages[0].payload.size() == enttec::kMaxPayload);
        CHECK(messages[1].payload.back() == 42);
    }
}
