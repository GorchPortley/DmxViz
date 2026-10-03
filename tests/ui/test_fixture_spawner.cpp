#include "stage/NodeFactory.h"
#include "stage/TrussProfile.h"
#include "ui/FixtureSpawner.h"

#include <doctest/doctest.h>

#include <filesystem>

using namespace dmxviz;
using namespace dmxviz::ui;

namespace {

// A spawner on an empty scene, with the bundled generic fixtures loaded.
struct SpawnRig {
    SpawnRig() {
        library.loadDirectory(std::filesystem::path(DMXVIZ_DATA_DIR) / "fixtures");
    }
    int fixtureCount() const { return static_cast<int>(scene.nodesOfKind(stage::NodeKind::Fixture).size()); }
    const stage::FixtureContent& content(NodeId id) const { return *scene.find(id)->as<stage::FixtureContent>(); }

    stage::Scene scene;
    stage::CommandStack commands{scene};
    stage::Selection selection;
    fixtures::FixtureLibrary library;
    FixtureSpawner spawner{scene, commands, selection, library};
};

stage::PickHit floorHit(float x, float z) {
    stage::PickHit hit;
    hit.point = {x, 0.0f, z};
    hit.normal = {0.0f, 1.0f, 0.0f};
    return hit;
}

}  // namespace

TEST_CASE("fixture spawner: a fixture dropped on the floor stands on it, patched and selected") {
    SpawnRig rig;
    const NodeId id = rig.spawner.addAtHit("generic/profile-spot", "Standard", floorHit(1.0f, 2.0f));
    REQUIRE(id != kInvalidNode);
    CHECK(rig.content(id).patch == stage::DmxPatch{1, 1});
    CHECK(rig.content(id).modeName == "Standard");
    CHECK(rig.content(id).fixtureNumber == 1);
    CHECK(rig.selection.primary() == id);

    const glm::mat4 world = rig.scene.worldMatrix(id);
    CHECK(world[3].x == doctest::Approx(1.0f));
    CHECK(world[3].y == doctest::Approx(0.0f));
    CHECK(world[3].z == doctest::Approx(2.0f));
    // The beam leaves along local -Y, so a fixture standing on the floor has local +Y pointing down.
    CHECK(world[1].y == doctest::Approx(-1.0f));
    // Its front faces the audience (+Z) like a hung fixture.
    CHECK(world[2].z == doctest::Approx(1.0f));
}

TEST_CASE("fixture spawner: new fixtures get consecutive free addresses and numbers") {
    SpawnRig rig;
    const NodeId a = rig.spawner.addAtHit("generic/profile-spot", "Standard", floorHit(0, 0));
    const NodeId b = rig.spawner.addAtHit("generic/rgb-par", "RGB", floorHit(1, 0));
    const NodeId c = rig.spawner.addAtHit("generic/rgb-par", "", floorHit(2, 0));  // empty mode: the first
    REQUIRE((a != kInvalidNode && b != kInvalidNode && c != kInvalidNode));
    CHECK(rig.content(a).patch == stage::DmxPatch{1, 1});
    CHECK(rig.content(b).patch == stage::DmxPatch{1, 30});  // after the 29 channels of the profile spot
    CHECK(rig.content(c).patch == stage::DmxPatch{1, 33});
    CHECK(rig.content(c).modeName == "RGB");
    CHECK(rig.content(c).fixtureNumber == 3);
}

TEST_CASE("fixture spawner: dropping on a truss hangs the fixture below it in one undo step") {
    SpawnRig rig;
    stage::Transform at;
    at.position = {0.0f, 5.0f, 0.0f};
    const NodeId truss = rig.scene.addNode(stage::factory::trussStraight(*stage::findTrussProfile("F34"), 4.0f), at);

    stage::PickHit hit;
    hit.node = truss;
    hit.point = {0.5f, 4.9f, 0.0f};
    const std::size_t undoBefore = rig.commands.undoCount();
    const NodeId id = rig.spawner.addAtHit("generic/led-wash-mover", "", hit);
    REQUIRE(id != kInvalidNode);
    CHECK(rig.commands.undoCount() == undoBefore + 1);  // create + hang
    const glm::mat4 world = rig.scene.worldMatrix(id);
    CHECK(world[3].y < 5.0f);
    CHECK(world[3].y > 4.5f);
    CHECK(world[3].x == doctest::Approx(0.5f).epsilon(0.2));
    CHECK(world[1].y == doctest::Approx(1.0f));  // hanging: local +Y up

    REQUIRE(rig.commands.undo());
    CHECK(rig.fixtureCount() == 0);
    REQUIRE(rig.commands.redo());
    CHECK(rig.fixtureCount() == 1);
}

TEST_CASE("fixture spawner: add to scene spreads fixtures out and unknown types are refused") {
    SpawnRig rig;
    const NodeId a = rig.spawner.addNearSelection("generic/par64-dimmer", "");
    const NodeId b = rig.spawner.addNearSelection("generic/par64-dimmer", "");
    REQUIRE((a != kInvalidNode && b != kInvalidNode));
    CHECK(glm::distance(glm::vec3(rig.scene.worldMatrix(a)[3]), glm::vec3(rig.scene.worldMatrix(b)[3])) > 0.4f);

    const int before = rig.fixtureCount();
    CHECK(rig.spawner.addNearSelection("nobody/nothing", "") == kInvalidNode);
    CHECK(rig.fixtureCount() == before);
}

TEST_CASE("fixture spawner: add to scene hangs on the selected truss") {
    SpawnRig rig;
    stage::Transform at;
    at.position = {0.0f, 5.0f, 0.0f};
    const NodeId truss = rig.scene.addNode(stage::factory::trussStraight(*stage::findTrussProfile("F34"), 4.0f), at);
    rig.selection.set(truss);
    const NodeId a = rig.spawner.addNearSelection("generic/par64-dimmer", "");
    REQUIRE(a != kInvalidNode);
    const glm::mat4 world = rig.scene.worldMatrix(a);
    CHECK(world[3].y < 5.0f);
    CHECK(world[3].y > 4.5f);
    CHECK(rig.selection.primary() == a);  // the new fixture is selected afterwards
}
