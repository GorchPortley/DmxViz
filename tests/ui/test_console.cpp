#include "stage/NodeFactory.h"
#include "ui/ConsoleSession.h"

#include <doctest/doctest.h>

#include <filesystem>

using namespace dmxviz;
using namespace dmxviz::ui;

namespace {

// Every test builds its own manager without an output thread.
struct Rig {
    Rig() : manager(dmx::DmxManagerOptions{.startOutputThread = false}), session(manager) {
        library.loadDirectory(std::filesystem::path(DMXVIZ_DATA_DIR) / "fixtures");
    }

    NodeId addFixture(const std::string& typeId, const std::string& mode, stage::DmxPatch patch) {
        return scene.addNode(stage::factory::fixture(typeId, mode, patch, 1));
    }
    std::uint8_t programmed(int universe, int address) {
        const auto values = manager.programmerValues(static_cast<dmx::UniverseId>(universe));
        return values ? (*values)[static_cast<std::size_t>(address - 1)] : std::uint8_t{0};
    }

    dmx::DmxManager manager;
    ConsoleSession session;
    fixtures::FixtureLibrary library;
    stage::Scene scene;
};

}  // namespace

TEST_CASE("programmer model: sets and releases single channels") {
    dmx::DmxManager manager(dmx::DmxManagerOptions{.startOutputThread = false});
    ProgrammerModel model(manager);

    model.setChannel(1, 5, 200);
    model.setChannel(1, 6, 100);
    CHECK(model.touched(1, 5));
    CHECK(model.value(1, 5) == 200);
    CHECK_FALSE(model.touched(1, 7));
    REQUIRE(manager.programmerValues(1).has_value());
    CHECK((*manager.programmerValues(1))[4] == 200);

    model.release(1, 5, 1);
    CHECK_FALSE(model.touched(1, 5));
    CHECK(model.touched(1, 6));
    CHECK((*manager.programmerValues(1))[4] == 0);
    CHECK((*manager.programmerValues(1))[5] == 100);  // the neighbour survives

    model.release(1, 6, 1);
    CHECK(model.empty());
    CHECK_FALSE(manager.programmerValues(1).has_value());
}

TEST_CASE("programmer model: ignores invalid addresses and clears everything") {
    dmx::DmxManager manager(dmx::DmxManagerOptions{.startOutputThread = false});
    ProgrammerModel model(manager);
    model.setChannel(0, 1, 1);
    model.setChannel(1, 0, 1);
    model.setChannel(1, 513, 1);
    CHECK(model.empty());

    model.setChannel(2, 512, 9);
    model.setChannel(3, 1, 9);
    CHECK(model.universes().size() == 2);
    model.releaseAll();
    CHECK(model.empty());
    CHECK_FALSE(manager.programmerValues(2).has_value());
}

TEST_CASE("console fixture: nothing is sent until the fixture is touched") {
    Rig rig;
    const NodeId id = rig.addFixture("generic/rgb-par", "RGB", {1, 10});
    ConsoleFixture* fixture = rig.session.fixture(rig.scene, rig.library, id);
    REQUIRE(fixture != nullptr);
    CHECK_FALSE(fixture->captured());
    fixture->send(rig.session.model());
    CHECK(rig.session.model().empty());
}

TEST_CASE("console fixture: RGB colour is mixed from the picked sRGB colour") {
    Rig rig;
    const NodeId id = rig.addFixture("generic/rgb-par", "RGB", {1, 10});
    ConsoleFixture* fixture = rig.session.fixture(rig.scene, rig.library, id);
    REQUIRE(fixture != nullptr);

    fixture->setColour({1.0f, 0.0f, 0.0f});
    fixture->send(rig.session.model());
    CHECK(fixture->captured());
    CHECK(rig.programmed(1, 10) == 255);
    CHECK(rig.programmed(1, 11) == 0);
    CHECK(rig.programmed(1, 12) == 0);
    CHECK(rig.session.model().touched(1, 12));  // the whole footprint is held
    CHECK_FALSE(rig.session.model().touched(1, 13));

    // No dimmer channel: the intensity fader dims the emitters.
    fixture->setIntensity(0.5f);
    fixture->send(rig.session.model());
    CHECK(rig.programmed(1, 10) < 255);
    CHECK(rig.programmed(1, 10) > 0);
}

TEST_CASE("console fixture: intensity, position and wheel slots use the fixture's own channels") {
    Rig rig;
    const NodeId id = rig.addFixture("generic/profile-spot", "Standard", {2, 100});
    ConsoleFixture* fixture = rig.session.fixture(rig.scene, rig.library, id);
    REQUIRE(fixture != nullptr);
    REQUIRE(fixture->encoder().has(fixtures::Attribute::Gobo1));

    fixture->setIntensity(0.0f);
    fixture->setPosition(1.0f, 0.0f, 0.0f, 0.0f);
    fixture->setWheelSlot(fixtures::Attribute::Gobo1, 3);
    fixture->send(rig.session.model());

    CHECK(rig.programmed(2, 105) == 0);    // dimmer (channel 6)
    CHECK(rig.programmed(2, 100) == 255);  // pan coarse at the end of its range
    CHECK(rig.programmed(2, 102) == 0);    // tilt coarse
    CHECK(rig.programmed(2, 110) > 0);     // gobo wheel moved off its first slot
    CHECK(fixture->values().slot[static_cast<std::size_t>(fixtures::Attribute::Gobo1)] == 3);

    // Colour: this fixture mixes CMY, so red means no cyan and full magenta and yellow.
    fixture->setColour({1.0f, 0.0f, 0.0f});
    fixture->send(rig.session.model());
    CHECK(rig.programmed(2, 106) == 0);
    CHECK(rig.programmed(2, 107) == 255);
    CHECK(rig.programmed(2, 108) == 255);
}

TEST_CASE("console fixture: colour wheel fallback picks the nearest slot") {
    fixtures::FixtureLibrary library;
    fixtures::FixtureType type;
    type.manufacturer = "Test";
    type.name = "Scroller";
    fixtures::Wheel wheel;
    wheel.name = "Colors";
    for (const glm::vec3 color : {glm::vec3(1.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)}) {
        fixtures::WheelSlot slot;
        slot.kind = fixtures::SlotKind::Color;
        slot.color = color;
        wheel.slots.push_back(slot);
    }
    type.wheels.push_back(wheel);
    fixtures::DmxMode mode;
    mode.name = "1ch";
    mode.footprint = 1;
    fixtures::Channel channel;
    channel.name = "Color";
    channel.offsets = {1};
    for (int slot = 1; slot <= 3; ++slot) {
        fixtures::ChannelFunction f;
        f.attribute = fixtures::Attribute::Color1;
        f.kind = fixtures::FunctionKind::WheelSlot;
        f.dmxFrom = static_cast<std::uint32_t>((slot - 1) * 10);
        f.dmxTo = f.dmxFrom + 9;
        f.wheel = "Colors";
        f.slotFrom = f.slotTo = static_cast<float>(slot);
        channel.functions.push_back(f);
    }
    mode.channels.push_back(channel);
    type.modes.push_back(mode);
    REQUIRE(library.addOrReplace(std::move(type)));

    dmx::DmxManager manager(dmx::DmxManagerOptions{.startOutputThread = false});
    ProgrammerModel model(manager);
    const fixtures::FixtureType* stored = library.find("test/scroller");
    REQUIRE(stored != nullptr);
    ConsoleFixture fixture(1, *stored, stored->modes.front(), {1, 1});

    fixture.setColour({0.0f, 0.0f, 1.0f});  // blue
    fixture.send(model);
    CHECK(model.value(1, 1) >= 20);
    CHECK(model.value(1, 1) <= 29);
    fixture.setColour({1.0f, 0.1f, 0.1f});  // reddish
    fixture.send(model);
    CHECK(model.value(1, 1) >= 10);
    CHECK(model.value(1, 1) <= 19);
}

TEST_CASE("console fixture: clear releases only this fixture, home resets it") {
    Rig rig;
    const NodeId a = rig.addFixture("generic/rgb-par", "RGB", {1, 1});
    const NodeId b = rig.addFixture("generic/rgb-par", "RGB", {1, 4});
    ConsoleFixture* fa = rig.session.fixture(rig.scene, rig.library, a);
    ConsoleFixture* fb = rig.session.fixture(rig.scene, rig.library, b);
    REQUIRE((fa != nullptr && fb != nullptr));
    fa->setColour({0.0f, 1.0f, 0.0f});
    fb->setColour({0.0f, 0.0f, 1.0f});
    fa->send(rig.session.model());
    fb->send(rig.session.model());
    CHECK(rig.session.capturedCount() == 2);

    fa->release(rig.session.model());
    CHECK_FALSE(fa->captured());
    CHECK_FALSE(rig.session.model().touched(1, 2));
    CHECK(rig.session.model().touched(1, 6));
    CHECK(rig.programmed(1, 6) == 255);  // fixture b still shows blue

    fb->home();
    fb->send(rig.session.model());
    CHECK(rig.programmed(1, 5) == 255);  // back to white
}

TEST_CASE("console session: follows re-patching and deleted nodes") {
    Rig rig;
    const NodeId id = rig.addFixture("generic/rgb-par", "RGB", {1, 1});
    ConsoleFixture* fixture = rig.session.fixture(rig.scene, rig.library, id);
    REQUIRE(fixture != nullptr);
    fixture->setColour({1.0f, 0.0f, 0.0f});
    fixture->send(rig.session.model());
    CHECK(rig.session.model().touched(1, 1));

    // Re-patch: the values move to the new address.
    stage::NodeData data = rig.scene.find(id)->data();
    data.as<stage::FixtureContent>()->patch = {1, 20};
    rig.scene.setData(id, data);
    fixture = rig.session.fixture(rig.scene, rig.library, id);
    REQUIRE(fixture != nullptr);
    CHECK_FALSE(rig.session.model().touched(1, 1));
    CHECK(rig.session.model().touched(1, 20));
    CHECK(rig.programmed(1, 20) == 255);

    // Delete: pruning takes the fixture out of the programmer.
    rig.scene.removeNode(id);
    rig.session.prune(rig.scene);
    CHECK(rig.session.model().empty());
    CHECK(rig.session.capturedCount() == 0);
}

TEST_CASE("console session: non-fixture nodes and unknown types give no console fixture") {
    Rig rig;
    const NodeId group = rig.scene.addNode(stage::factory::group("G"));
    const NodeId unknown = rig.addFixture("nobody/nothing", "x", {1, 1});
    CHECK(rig.session.fixture(rig.scene, rig.library, group) == nullptr);
    CHECK(rig.session.fixture(rig.scene, rig.library, unknown) == nullptr);
    CHECK(rig.session.fixture(rig.scene, rig.library, 9999) == nullptr);
}
