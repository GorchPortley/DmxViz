#include "ui/DmxTextFormat.h"

#include <doctest/doctest.h>

using namespace dmxviz;
using namespace dmxviz::ui;

TEST_CASE("universe list: format collapses runs") {
    CHECK(formatUniverseList(std::vector<std::uint16_t>{}).empty());
    CHECK(formatUniverseList(std::vector<std::uint16_t>{1, 2, 3, 4, 7}) == "1-4, 7");
    CHECK(formatUniverseList(std::vector<std::uint16_t>{5, 3, 3, 4}) == "3-5");
    CHECK(formatUniverseList(std::vector<std::uint16_t>{1, 2, 9}) == "1, 2, 9");
}

TEST_CASE("universe list: parse accepts numbers, ranges and mixed separators") {
    std::vector<std::uint16_t> out;
    REQUIRE(parseUniverseList("1-3, 7 9;3", 1, 100, out));
    CHECK(out == std::vector<std::uint16_t>{1, 2, 3, 7, 9});
    REQUIRE(parseUniverseList("  ", 1, 100, out));
    CHECK(out.empty());
}

TEST_CASE("universe list: invalid text leaves the output alone") {
    std::vector<std::uint16_t> out{5};
    CHECK_FALSE(parseUniverseList("1-", 1, 100, out));
    CHECK_FALSE(parseUniverseList("abc", 1, 100, out));
    CHECK_FALSE(parseUniverseList("4-2", 1, 100, out));
    CHECK_FALSE(parseUniverseList("0", 1, 100, out));
    CHECK_FALSE(parseUniverseList("101", 1, 100, out));
    CHECK(out == std::vector<std::uint16_t>{5});
}

TEST_CASE("endpoint list: round trip with a default port") {
    std::vector<dmx::Endpoint> out;
    REQUIRE(parseEndpointList("192.168.1.20, 10.0.0.5:6455", 6454, out));
    REQUIRE(out.size() == 2);
    CHECK(out[0].port == 6454);
    CHECK(out[1].port == 6455);
    CHECK(formatEndpointList(out, 6454) == "192.168.1.20, 10.0.0.5:6455");
}

TEST_CASE("endpoint list: rejects garbage") {
    std::vector<dmx::Endpoint> out;
    CHECK_FALSE(parseEndpointList("192.168.1", 6454, out));
    CHECK_FALSE(parseEndpointList("1.2.3.4, nope", 6454, out));
}
