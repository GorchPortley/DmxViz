#include "stage/NodeFactory.h"
#include "ui/ChannelOwnerMap.h"

#include <doctest/doctest.h>

using namespace dmxviz;
using namespace dmxviz::ui;

namespace {

// "test/mover" with a 4-slot mode: dimmer (slot 1), pan 16-bit (slots 2+3), strobe (slot 4).
fixtures::FixtureLibrary makeLibrary() {
    fixtures::FixtureLibrary library;
    fixtures::FixtureType type;
    type.manufacturer = "Test";
    type.name = "Mover";
    fixtures::DmxMode mode;
    mode.name = "4ch";
    mode.footprint = 4;
    mode.channels.push_back({.name = "Dimmer", .offsets = {1}});
    mode.channels.push_back({.name = "Pan", .offsets = {2, 3}});
    mode.channels.push_back({.name = "Strobe", .offsets = {4}});
    type.modes.push_back(mode);
    REQUIRE(library.addOrReplace(std::move(type)));
    return library;
}

}  // namespace

TEST_CASE("channel owner map: maps slots to channels and bytes") {
    const fixtures::FixtureLibrary library = makeLibrary();
    stage::Scene scene;
    const NodeId a = scene.addNode(stage::factory::fixture("test/mover", "4ch", {1, 10}, 1));
    scene.addNode(stage::factory::fixture("test/mover", "4ch", {2, 1}, 2));  // other universe

    ChannelOwnerMap map;
    map.build(scene, library, 1);
    CHECK_FALSE(map.at(9).used());
    REQUIRE(map.at(10).used());
    CHECK(map.at(10).fixture == a);
    CHECK(map.at(10).channel == 0);
    CHECK(map.at(10).first);
    CHECK(map.at(11).channel == 1);
    CHECK(map.at(11).byte == 0);
    CHECK(map.at(12).channel == 1);
    CHECK(map.at(12).byte == 1);  // fine byte of pan
    CHECK_FALSE(map.at(12).first);
    CHECK(map.at(13).channel == 2);
    CHECK_FALSE(map.at(14).used());
    CHECK_FALSE(map.at(0).used());
    CHECK_FALSE(map.at(513).used());
    CHECK_FALSE(map.at(10).shared);
}

TEST_CASE("channel owner map: overlapping fixtures are flagged as shared") {
    const fixtures::FixtureLibrary library = makeLibrary();
    stage::Scene scene;
    scene.addNode(stage::factory::fixture("test/mover", "4ch", {1, 1}, 1));
    scene.addNode(stage::factory::fixture("test/mover", "4ch", {1, 3}, 2));

    ChannelOwnerMap map;
    map.build(scene, library, 1);
    CHECK_FALSE(map.at(2).shared);
    CHECK(map.at(3).shared);
    CHECK(map.at(4).shared);
    CHECK_FALSE(map.at(6).shared);
}

TEST_CASE("channel owner map: slots beyond 512 are ignored") {
    const fixtures::FixtureLibrary library = makeLibrary();
    stage::Scene scene;
    scene.addNode(stage::factory::fixture("test/mover", "4ch", {1, 511}, 1));

    ChannelOwnerMap map;
    map.build(scene, library, 1);
    CHECK(map.at(511).used());
    CHECK(map.at(512).used());
}
