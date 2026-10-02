#include "assets/AssetLibrary.h"
#include "stage/CommandStack.h"
#include "stage/Commands.h"
#include "stage/NodeFactory.h"
#include "stage/PlacementTools.h"
#include "stage/Scene.h"

#include <doctest/doctest.h>

using namespace dmxviz;
using namespace dmxviz::stage;
using assets::AssetLibrary;

namespace {

Transform at(float x, float y, float z, float yawDeg = 0.0f) {
    Transform t;
    t.position = {x, y, z};
    t.setEulerDegrees({0.0f, yawDeg, 0.0f});
    return t;
}

glm::vec3 worldPos(const Scene& s, NodeId id) { return glm::vec3(s.worldMatrix(id)[3]); }
glm::vec3 worldDir(const Scene& s, NodeId id, const glm::vec3& local) {
    return glm::normalize(glm::vec3(s.worldMatrix(id) * glm::vec4(local, 0.0f)));
}

void checkVec(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) {
    CHECK(a.x == doctest::Approx(b.x).epsilon(eps));
    CHECK(a.y == doctest::Approx(b.y).epsilon(eps));
    CHECK(a.z == doctest::Approx(b.z).epsilon(eps));
}

NodeId addBox(Scene& s, const Transform& t, glm::vec3 size = glm::vec3(1.0f), NodeId parent = kInvalidNode) {
    return s.addNode(factory::primitive(PrimitiveShape::Box, size), t, parent);
}

}  // namespace

TEST_CASE("snapping helpers") {
    CHECK(tools::snapValue(0.37f, 0.25f) == doctest::Approx(0.25f));
    CHECK(tools::snapValue(0.38f, 0.25f) == doctest::Approx(0.5f));
    CHECK(tools::snapValue(0.37f, 0.0f) == doctest::Approx(0.37f));
    checkVec(tools::snapToGrid({0.12f, -0.9f, 2.26f}, 0.5f), {0.0f, -1.0f, 2.5f});
    CHECK(tools::snapAngleDeg(22.0f, 15.0f) == doctest::Approx(15.0f));
    CHECK(tools::snapAngleDeg(23.0f, 15.0f) == doctest::Approx(30.0f));

    Transform t = at(0.13f, 1.01f, -0.4f, 47.0f);
    const Transform snapped = tools::applySnap(t, {});
    checkVec(snapped.position, {0.25f, 1.0f, -0.5f});
    CHECK(snapped.eulerDegrees().y == doctest::Approx(45.0f));
    tools::SnapSettings off;
    off.grid = false;
    off.angle = false;
    CHECK(tools::applySnap(t, off) == t);

    PickHit hit;
    hit.point = {0.9f, 0.0f, 0.1f};
    hit.triangleWorld = {glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(0, 0, 1)};
    checkVec(tools::nearestVertex(hit), {1, 0, 0});
    hit.normal = glm::normalize(glm::vec3(1, 1, 0));
    const glm::mat4 placed = tools::placeOnSurface(hit, glm::mat4(1.0f), true);
    checkVec(glm::vec3(placed[3]), hit.point);
    checkVec(glm::normalize(glm::vec3(placed[1])), hit.normal);
    const glm::mat4 moved = tools::placeOnSurface(hit, glm::mat4(1.0f), false);
    checkVec(glm::vec3(moved[1]), {0, 1, 0});
}

TEST_CASE("linear and grid arrays") {
    Scene s;
    CommandStack stack(s);
    const NodeId a = addBox(s, at(0, 0, 0));
    Command* cmd = stack.execute(tools::linearArray(s, {a}, 3, {1.5f, 0, 0}));
    REQUIRE(cmd);
    CHECK(s.nodeCount() == 4);
    const std::vector<NodeId> roots = s.roots();
    CHECK(roots[0] == a);
    for (int i = 1; i < 4; ++i) checkVec(worldPos(s, roots[static_cast<std::size_t>(i)]), {1.5f * float(i), 0, 0});
    stack.undo();
    CHECK(s.nodeCount() == 1);

    // Offsets are in world space even under a rotated parent.
    const NodeId g = s.addNode(factory::group(), at(0, 0, 0, 90.0f));
    const NodeId child = addBox(s, at(1, 0, 0), glm::vec3(1.0f), g);
    stack.execute(tools::linearArray(s, {child}, 1, {0, 0, -1}));
    REQUIRE(s.childrenOf(g).size() == 2);
    checkVec(worldPos(s, s.childrenOf(g)[1]), worldPos(s, child) + glm::vec3(0, 0, -1));
    CHECK(s.find(s.childrenOf(g)[1])->transform().rotation == s.find(child)->transform().rotation);

    Scene grid;
    const NodeId b = addBox(grid, at(1, 0, 1));
    REQUIRE(tools::gridArray(grid, {b}, {3, 1, 2}, {2, 0, 3})->apply(grid));
    CHECK(grid.nodeCount() == 6);
    bool found = false;
    for (NodeId id : grid.roots()) {
        if (glm::distance(worldPos(grid, id), glm::vec3(5, 0, 4)) < 1e-4f) found = true;
    }
    CHECK(found);
    CHECK(tools::linearArray(grid, {}, 3, {1, 0, 0}) == nullptr);
}

TEST_CASE("circular arrays: full circle and arc, with and without rotation") {
    Scene s;
    const NodeId a = addBox(s, at(2, 0, 0));
    tools::CircularArraySettings c;
    c.count = 4;
    REQUIRE(tools::circularArray(s, {a}, c)->apply(s));
    REQUIRE(s.nodeCount() == 4);
    const std::vector<NodeId> r = s.roots();
    checkVec(worldPos(s, r[1]), {0, 0, -2});  // +90 deg about +Y
    checkVec(worldPos(s, r[2]), {-2, 0, 0});
    checkVec(worldPos(s, r[3]), {0, 0, 2});
    checkVec(worldDir(s, r[1], {1, 0, 0}), {0, 0, -1});  // copies turn with the circle

    Scene arc;
    const NodeId b = addBox(arc, at(0, 0, 3));
    tools::CircularArraySettings half;
    half.count = 3;
    half.totalAngleDeg = 180.0f;
    half.rotateCopies = false;
    REQUIRE(tools::circularArray(arc, {b}, half)->apply(arc));
    const std::vector<NodeId> ra = arc.roots();
    REQUIRE(ra.size() == 3);
    checkVec(worldPos(arc, ra[1]), {3, 0, 0});
    checkVec(worldPos(arc, ra[2]), {0, 0, -3});
    checkVec(worldDir(arc, ra[2], {1, 0, 0}), {1, 0, 0});  // orientation kept
}

TEST_CASE("align and distribute use world bounds") {
    AssetLibrary lib;
    Scene s;
    const NodeId a = addBox(s, at(0, 0, 0), {1, 1, 1});
    const NodeId b = addBox(s, at(5, 2, 0), {2, 1, 1});
    const NodeId c = addBox(s, at(9, 1, 0), {1, 3, 1});
    CommandStack stack(s);
    REQUIRE(stack.execute(tools::alignNodes(s, lib, {a, b, c}, tools::Axis::X, tools::AlignMode::Min)));
    for (NodeId id : {a, b, c}) CHECK(s.worldBounds(id, lib).min.x == doctest::Approx(-0.5f));
    stack.undo();
    REQUIRE(stack.execute(tools::alignNodes(s, lib, {a, b, c}, tools::Axis::Y, tools::AlignMode::Max, 4.0f)));
    for (NodeId id : {a, b, c}) CHECK(s.worldBounds(id, lib).max.y == doctest::Approx(4.0f));
    stack.undo();
    REQUIRE(stack.execute(tools::alignNodes(s, lib, {b, c}, tools::Axis::Z, tools::AlignMode::Centre)));
    stack.undo();

    // Centres: 0 .. 9, so b's centre moves to 4.5.
    REQUIRE(stack.execute(tools::distributeNodes(s, lib, {c, a, b}, tools::Axis::X, tools::DistributeMode::Centres)));
    CHECK(s.worldBounds(b, lib).center().x == doctest::Approx(4.5f));
    CHECK(s.worldBounds(a, lib).center().x == doctest::Approx(0.0f));
    stack.undo();
    // Gaps: a spans -0.5..0.5, c spans 8.5..9.5, b is 2 wide: gaps of 3 m.
    REQUIRE(stack.execute(tools::distributeNodes(s, lib, {a, b, c}, tools::Axis::X, tools::DistributeMode::Gaps)));
    CHECK(s.worldBounds(b, lib).min.x == doctest::Approx(3.5f));
    CHECK(tools::distributeNodes(s, lib, {a, b}, tools::Axis::X, tools::DistributeMode::Gaps) == nullptr);
}

TEST_CASE("mirror: positions and orientations reflect, subtrees follow, copies are optional") {
    Scene s;
    const NodeId fx = s.addNode(factory::fixture("generic/spot", "16ch"), at(2, 3, 1, 30.0f));
    const NodeId g = s.addNode(factory::group(), at(4, 0, 0));
    const NodeId kid = addBox(s, at(1, 0, 0), glm::vec3(1.0f), g);
    const glm::vec3 forward = worldDir(s, fx, {0, 0, 1});
    CommandStack stack(s);
    REQUIRE(stack.execute(tools::mirrorNodes(s, {fx, g}, tools::Axis::X, 0.0f, false)));
    checkVec(worldPos(s, fx), {-2, 3, 1});
    checkVec(worldDir(s, fx, {0, 0, 1}), {-forward.x, forward.y, forward.z});
    CHECK(glm::determinant(glm::mat3(s.worldMatrix(fx))) > 0.0f);  // still a proper rotation
    checkVec(worldPos(s, g), {-4, 0, 0});
    checkVec(worldPos(s, kid), {-5, 0, 0});
    stack.undo();
    checkVec(worldPos(s, kid), {5, 0, 0});

    REQUIRE(stack.execute(tools::mirrorNodes(s, {g}, tools::Axis::Z, 2.0f, true)));
    CHECK(s.nodeCount() == 5);
    const NodeId copy = s.roots()[2];
    checkVec(worldPos(s, copy), {4, 0, 4});
    checkVec(worldPos(s, s.childrenOf(copy)[0]), {5, 0, 4});
    checkVec(worldPos(s, kid), {5, 0, 0});  // original untouched
}

TEST_CASE("drop to floor and onto other geometry") {
    AssetLibrary lib;
    Scene s;
    const NodeId box = addBox(s, at(0, 5, 0));
    const NodeId deck = s.addNode(factory::stageDeck(2, 2, 1.0f), at(0, 0, 0));
    REQUIRE(tools::dropToFloor(s, lib, {box})->apply(s));
    CHECK(s.worldBounds(box, lib).min.y == doctest::Approx(0.0f));

    s.setTransform(box, at(0.5f, 5, 0.2f));
    std::vector<MeshInstance> obstacles;
    s.collectRenderables(lib, obstacles);
    REQUIRE(tools::dropToFloor(s, lib, {box}, 0.0f, &obstacles)->apply(s));
    CHECK(s.worldBounds(box, lib).min.y == doctest::Approx(1.0f));  // resting on the deck top
    // Beside the deck it goes to the floor.
    s.setTransform(box, at(10, 5, 0));
    REQUIRE(tools::dropToFloor(s, lib, {box}, 0.0f, &obstacles)->apply(s));
    CHECK(s.worldBounds(box, lib).min.y == doctest::Approx(0.0f));
    (void)deck;
}

TEST_CASE("hang on truss: clamps under the nearest bottom chord, facing a direction") {
    Scene s;
    const TrussProfile& f34 = *findTrussProfile("F34");
    const NodeId truss = s.addNode(factory::trussStraight(f34, 4.0f), at(0, 6, 0));
    const NodeId fx = s.addNode(factory::fixture("generic/wash", "14ch"), at(0, 0, 0));
    const float h = f34.chordSpacing() * 0.5f;
    const float r = f34.chordDiameter * 0.5f;

    tools::HangOptions opts;
    auto hang = tools::hangOnTruss(s, fx, truss, {1.2f, 6.0f, 0.3f}, opts);
    REQUIRE(hang);
    checkVec(hang->chordPoint, {1.2f, 6.0f - h, h});
    checkVec(hang->local.position, {1.2f, 6.0f - h - r - opts.clampDrop, h});
    checkVec(glm::vec3(glm::mat4_cast(hang->local.rotation) * glm::vec4(0, 0, 1, 0)), {0, 0, 1});
    checkVec(hang->chordDirection, {1, 0, 0});

    // Facing +X turns local +Z to +X; snapping along the chord in 0.5 m steps.
    opts.facing = {1, 0, 0};
    opts.snapAlong = 0.5f;
    hang = tools::hangOnTruss(s, fx, truss, {1.2f, 6.0f, -0.3f}, opts);
    REQUIRE(hang);
    checkVec(hang->chordPoint, {1.0f, 6.0f - h, -h});
    checkVec(glm::vec3(hang->world * glm::vec4(0, 0, 1, 0)), {1, 0, 0});
    checkVec(glm::vec3(hang->world * glm::vec4(0, 1, 0, 0)), {0, 1, 0});  // hanging: no pitch or roll

    // A truss turned upside down (rolled 180 deg) still hangs from its lowest chords.
    Transform rolled = at(0, 6, 0);
    rolled.setEulerDegrees({180, 0, 0});
    s.setTransform(truss, rolled);
    hang = tools::hangOnTruss(s, fx, truss, {0, 6, 0.3f}, {});
    REQUIRE(hang);
    CHECK(hang->chordPoint.y == doctest::Approx(6.0f - h));

    // Non-truss targets are refused.
    CHECK_FALSE(tools::hangOnTruss(s, fx, fx, {0, 0, 0}, {}));
}

TEST_CASE("hang on truss command: reparent under the truss, undo restores") {
    Scene s;
    const NodeId truss = s.addNode(factory::trussStraight(*findTrussProfile("F34"), 3.0f), at(0, 5, -2, 90.0f));
    const NodeId fx = s.addNode(factory::fixture("generic/spot", "1ch"), at(3, 0, 3));
    CommandStack stack(s);
    tools::HangOptions opts;
    opts.reparent = true;
    REQUIRE(stack.execute(tools::hangOnTrussCommand(s, fx, truss, {0.2f, 5.0f, -2.5f}, opts)));
    CHECK(s.find(fx)->parent() == truss);
    const glm::vec3 p = worldPos(s, fx);
    CHECK(p.y < 5.0f);
    CHECK(p.z == doctest::Approx(-2.5f).epsilon(1e-3));  // the run goes along world Z after the 90 deg yaw
    checkVec(worldDir(s, fx, {0, 0, 1}), {0, 0, 1});    // faces the audience regardless of the truss yaw
    CHECK(stack.undoCount() == 1);
    stack.undo();
    CHECK(s.find(fx)->parent() == kInvalidNode);
    checkVec(worldPos(s, fx), {3, 0, 3});
}

TEST_CASE("hang on a tower clamps on the outside of a vertical chord") {
    Scene s;
    const NodeId tower = s.addNode(factory::trussTower(*findTrussProfile("F34"), 5.0f, 4.0f), at(0, 0, 0));
    const NodeId fx = s.addNode(factory::fixture("generic/par", "3ch"));
    auto hang = tools::hangOnTruss(s, fx, tower, {0.5f, 2.0f, 0.5f}, {});
    REQUIRE(hang);
    CHECK(hang->chordPoint.y == doctest::Approx(2.0f));
    const glm::vec3 p = glm::vec3(hang->world[3]);
    CHECK(p.x > hang->chordPoint.x);
    CHECK(p.z > hang->chordPoint.z);
}
