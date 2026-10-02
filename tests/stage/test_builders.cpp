#include "assets/AssetLibrary.h"
#include "assets/ModelLoader.h"
#include "assets/Primitives.h"
#include "stage/NodeFactory.h"
#include "stage/Scene.h"
#include "stage/Selection.h"
#include "stage/SetBuilder.h"
#include "stage/TrussBuilder.h"

#include <doctest/doctest.h>

#include <numeric>

using namespace dmxviz;
using namespace dmxviz::stage;
using assets::AssetLibrary;

namespace {

Aabb partsBounds(const std::vector<RenderPart>& parts, const AssetLibrary& lib) {
    Aabb b;
    for (const RenderPart& p : parts) b.expand(lib.mesh(p.mesh)->bounds.transformed(p.local));
    return b;
}

TrussContent makeTruss(TrussPiece piece, const TrussProfile& profile = *findTrussProfile("F34")) {
    TrussContent t;
    t.profile = profile;
    t.piece = piece;
    return t;
}

}  // namespace

TEST_CASE("truss: standard profiles and run splitting") {
    REQUIRE(findTrussProfile("F34"));
    CHECK(findTrussProfile("F34")->width == doctest::Approx(0.29f));
    CHECK(findTrussProfile("F44")->width == doctest::Approx(0.39f));
    CHECK(findTrussProfile("F33")->shape == TrussShape::Triangle);
    CHECK(findTrussProfile("nope") == nullptr);
    CHECK(findTrussProfile("F34")->chordPositions().size() == 4);
    CHECK(findTrussProfile("F33")->chordPositions().size() == 3);
    CHECK(findTrussProfile("F32")->chordPositions().size() == 2);

    CHECK(truss::splitRun(7.0f) == std::vector<float>{4.0f, 3.0f});
    CHECK(truss::splitRun(10.0f) == std::vector<float>{4.0f, 4.0f, 2.0f});
    const std::vector<float> odd = truss::splitRun(3.7f);
    REQUIRE(odd.size() == 2);
    CHECK(odd[0] == doctest::Approx(3.0f));
    CHECK(odd[1] == doctest::Approx(0.7f));  // short remainder merged, never a stub < 0.5 m
    CHECK(truss::splitRun(0.3f).size() == 1);
    CHECK(truss::splitRun(0.0f).empty());
    CHECK(truss::splitRun(5.0f, {2.0f, 1.0f, 2.0f}) == std::vector<float>{2.0f, 1.0f, 2.0f});
}

TEST_CASE("truss: straight segment geometry, triangle budget and mesh sharing") {
    AssetLibrary lib;
    const TrussProfile& f34 = *findTrussProfile("F34");
    const assets::MeshData seg = truss::buildStraight(f34, 3.0f);
    Aabb b = seg.bounds;
    if (b.empty()) {
        assets::MeshData copy = seg;
        copy.computeBounds();
        b = copy.bounds;
    }
    CHECK(b.min.x == doctest::Approx(-1.5f));
    CHECK(b.max.x == doctest::Approx(1.5f));
    CHECK(b.max.y == doctest::Approx(0.145f).epsilon(0.01));
    CHECK(b.min.z == doctest::Approx(-0.145f).epsilon(0.01));
    CHECK(seg.triangleCount() > 300);
    CHECK(seg.triangleCount() < 3000);

    const MeshId a = truss::straightMesh(lib, f34, 3.0f);
    const std::size_t meshes = lib.meshCount();
    CHECK(truss::straightMesh(lib, f34, 3.0f) == a);        // cache hit
    CHECK(truss::straightMesh(lib, f34, 3.00001f) == a);    // same to 0.1 mm
    CHECK(lib.meshCount() == meshes);
    CHECK(truss::straightMesh(lib, f34, 2.0f) != a);
    CHECK(truss::straightMesh(lib, *findTrussProfile("F44"), 3.0f) != a);

    // A 7 m run uses a 4 m and a 3 m segment; the 3 m one is the cached mesh.
    TrussContent run = makeTruss(TrussPiece::Straight);
    run.straight.length = 7.0f;
    const std::vector<RenderPart> parts = truss::trussParts(run, lib);
    REQUIRE(parts.size() == 2);
    CHECK(parts[1].mesh == a);
    const Aabb rb = partsBounds(parts, lib);
    CHECK(rb.min.x == doctest::Approx(-3.5f));
    CHECK(rb.max.x == doctest::Approx(3.5f));
}

TEST_CASE("truss: triangle, ladder and flat profiles") {
    AssetLibrary lib;
    const TrussProfile& tri = *findTrussProfile("F33");
    const assets::MeshData t = truss::buildStraight(tri, 2.0f);
    assets::MeshData tc = t;
    tc.computeBounds();
    const float s = tri.chordSpacing();
    CHECK(tc.bounds.size().y == doctest::Approx(s * std::sqrt(3.0f) * 0.5f + tri.chordDiameter).epsilon(0.01));
    CHECK(tc.bounds.size().z == doctest::Approx(tri.width).epsilon(0.01));
    for (const char* name : {"F32", "F32 flat"}) {
        assets::MeshData m = truss::buildStraight(*findTrussProfile(name), 2.0f);
        m.computeBounds();
        CHECK(m.triangleCount() > 100);
        CHECK(m.triangleCount() < t.triangleCount());
    }
}

TEST_CASE("truss: corner blocks, arcs and towers") {
    AssetLibrary lib;
    TrussContent corner = makeTruss(TrussPiece::Corner);
    corner.corner.faces = trussCornerFaces(TrussCornerPreset::TwoWay);
    const auto cp = truss::trussParts(corner, lib);
    REQUIRE(cp.size() == 1);
    const Aabb cb = partsBounds(cp, lib);
    CHECK(cb.size().x == doctest::Approx(0.29f).epsilon(0.02));
    CHECK(cb.size().y == doctest::Approx(0.29f).epsilon(0.02));
    // Different face masks are different meshes; same masks share.
    CHECK(truss::cornerMesh(lib, corner.profile, trussCornerFaces(TrussCornerPreset::SixWay)) != cp[0].mesh);
    CHECK(truss::cornerMesh(lib, corner.profile, corner.corner.faces) == cp[0].mesh);
    CHECK(lib.mesh(truss::cornerMesh(lib, corner.profile, 63))->triangleCount() <
          lib.mesh(cp[0].mesh)->triangleCount());  // open faces have no braces

    TrussContent circle = makeTruss(TrussPiece::Arc);
    circle.arc.radius = 3.0f;
    circle.arc.angleDeg = 360.0f;
    circle.arc.pieces = 4;
    const auto ap = truss::trussParts(circle, lib);
    REQUIRE(ap.size() == 4);
    for (const RenderPart& p : ap) CHECK(p.mesh == ap[0].mesh);  // one mesh, four instances
    const Aabb ab = partsBounds(ap, lib);
    const float outer = 3.0f + 0.145f;
    CHECK(ab.max.x == doctest::Approx(outer).epsilon(0.01));
    CHECK(ab.min.x == doctest::Approx(-outer).epsilon(0.01));
    CHECK(ab.max.z == doctest::Approx(outer).epsilon(0.01));
    CHECK(ab.min.z == doctest::Approx(-outer).epsilon(0.01));

    // A 90 degree arc goes from +X toward -Z.
    TrussContent quarter = circle;
    quarter.arc.angleDeg = 90.0f;
    quarter.arc.pieces = 1;
    const Aabb qb = partsBounds(truss::trussParts(quarter, lib), lib);
    CHECK(qb.min.z < -2.5f);
    CHECK(qb.max.z < 0.5f);
    CHECK(qb.min.x > -0.5f);

    TrussContent tower = makeTruss(TrussPiece::Tower);
    tower.tower.height = 6.0f;
    tower.tower.sleeveHeight = 4.5f;
    tower.tower.basePlateSize = 1.0f;
    const auto tp = truss::trussParts(tower, lib);
    CHECK(tp.size() >= 4);  // base plate, truss segments, top block, sleeve
    const Aabb tb = partsBounds(tp, lib);
    CHECK(tb.min.y == doctest::Approx(0.0f).epsilon(1e-3));
    CHECK(tb.max.y == doctest::Approx(6.0f).epsilon(0.01));
    CHECK(tb.size().x == doctest::Approx(1.0f).epsilon(0.01));  // base plate is the widest part
    // Chords of a tower are vertical lines within its height.
    for (const truss::ChordLine& c : truss::trussChords(tower)) {
        CHECK(c.a.x == doctest::Approx(c.b.x));
        CHECK(c.b.y > c.a.y);
    }
}

TEST_CASE("set pieces: decks, risers, steps, walls, flats and the reference figure") {
    AssetLibrary lib;
    StageDeckContent deck;
    deck.columns = 3;
    deck.rows = 2;
    deck.height = 1.0f;
    deck.skirt = true;
    const auto dp = setpiece::deckParts(deck, lib);
    CHECK(dp.size() == 6 + 6 + 4);  // panels, leg sets, skirt sides
    const Aabb db = partsBounds(dp, lib);
    CHECK(db.min.y == doctest::Approx(0.0f));
    CHECK(db.max.y == doctest::Approx(1.0f));
    CHECK(db.size().x == doctest::Approx(6.0f).epsilon(0.01));
    CHECK(db.size().z == doctest::Approx(2.0f).epsilon(0.01));
    CHECK(dp[0].mesh == dp[2].mesh);  // all panels share one mesh

    StageDeckContent riser = deck;
    riser.style = DeckStyle::Riser;
    riser.skirt = false;
    CHECK(setpiece::deckParts(riser, lib).size() == 6 + 4);  // no legs, fascia on four sides

    StepsContent steps;
    steps.width = 1.2f;
    steps.height = 0.8f;
    CHECK(steps.effectiveSteps() == 4);
    const auto sp = setpiece::stepsParts(steps, lib);
    const Aabb sb = partsBounds(sp, lib);
    CHECK(sb.max.y == doctest::Approx(0.8f));
    CHECK(sb.size().z == doctest::Approx(4 * 0.3f));
    CHECK(sb.size().x == doctest::Approx(1.2f));
    const MeshId stepMesh = sp[0].mesh;
    CHECK(setpiece::stepsParts(steps, lib)[0].mesh == stepMesh);

    WallContent wall;
    const Aabb wb = partsBounds(setpiece::wallParts(wall, lib), lib);
    CHECK(wb.max.y == doctest::Approx(3.0f));
    CHECK(wb.size().x == doctest::Approx(4.0f));
    wall.style = WallStyle::Flat;
    const auto fp = setpiece::wallParts(wall, lib);
    CHECK(fp.size() == 2);  // skin + frame
    CHECK(partsBounds(fp, lib).min.y == doctest::Approx(0.0f).epsilon(1e-3));

    ReferenceFigureContent person;
    Aabb pb = partsBounds(setpiece::figureParts(person, lib), lib);
    CHECK(pb.max.y == doctest::Approx(1.8f).epsilon(0.01));
    CHECK(pb.min.y == doctest::Approx(0.0f).epsilon(1e-3));
    person.height = 1.6f;
    pb = partsBounds(setpiece::figureParts(person, lib), lib);
    CHECK(pb.max.y == doctest::Approx(1.6f).epsilon(0.01));
}

TEST_CASE("collectRenderables: world instances, pick ids, visibility, layers and highlight") {
    AssetLibrary lib;
    Scene s;
    Transform up;
    up.position = {0, 5, 0};
    const NodeId g = s.addNode(factory::group("Rig"), up);
    const NodeId t = s.addNode(factory::trussStraight(*findTrussProfile("F34"), 7.0f), {}, g);
    const NodeId box = s.addNode(factory::primitive(PrimitiveShape::Box, {2, 1, 1}));
    s.addNode(factory::fixture("generic/par", "RGB"), {}, t);  // drawn by the simulation, not the stage
    s.addNode(factory::cameraPreset("FOH"));

    std::vector<MeshInstance> out;
    s.collectRenderables(lib, out);
    REQUIRE(out.size() == 3);  // 2 truss segments + box
    CHECK(out[0].pickId == t);
    CHECK(out[2].pickId == box);
    CHECK(out[0].world[3].y == doctest::Approx(5.0f));
    CHECK(out[2].world[0].x == doctest::Approx(2.0f));  // primitive size is a scale on the unit cube
    for (const MeshInstance& i : out) CHECK(i.highlight == Highlight::None);

    // Selecting the group highlights its subtree; hover on the box.
    Selection sel;
    sel.set(g);
    sel.setHover(box);
    out.clear();
    s.collectRenderables(lib, out, &sel);
    CHECK(out[0].highlight == Highlight::Selected);
    CHECK(out[2].highlight == Highlight::Hover);

    // Hidden group, hidden layer.
    NodeData gd = s.find(g)->data();
    gd.visible = false;
    s.setData(g, gd);
    out.clear();
    s.collectRenderables(lib, out);
    CHECK(out.size() == 1);
    s.setLayers({Layer{"Default"}, Layer{"Hidden", false}});
    NodeData bd = s.find(box)->data();
    bd.layer = 1;
    bd.materialOverride = Material{glm::vec3(1, 0, 0), 0.5f, 0.0f, glm::vec3(0)};
    s.setData(box, bd);
    out.clear();
    s.collectRenderables(lib, out);
    CHECK(out.empty());
    s.setLayers({Layer{"Default"}, Layer{"Shown"}});
    out.clear();
    s.collectRenderables(lib, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].material.albedo.x == doctest::Approx(1.0f));  // node material override

    // Bounds: the box (2 x 1 x 1 at the origin) and a fixture-only node (its position).
    const Aabb bb = s.worldBounds(box, lib);
    CHECK(bb.size().x == doctest::Approx(2.0f));
    const NodeId fx = s.nodesOfKind(NodeKind::Fixture).front();
    const Aabb fb = s.worldBounds(fx, lib);
    CHECK(fb.min.y == doctest::Approx(5.0f));
    CHECK(fb.size().x == doctest::Approx(0.0f));
}

TEST_CASE("Model nodes use the model cache; missing files show a marker") {
    AssetLibrary lib;
    Scene s;
    ModelContent good;
    good.path = "virtual/speaker.glb";
    assets::ModelData data;
    assets::ModelPart part;
    part.mesh = assets::makeBox({0.5f, 1.0f, 0.4f});
    part.material.albedo = glm::vec3(0.1f, 0.2f, 0.3f);
    part.transform = glm::translate(glm::mat4(1.0f), {0, 0.5f, 0});
    data.parts.push_back(part);
    data.bounds = part.mesh.bounds.transformed(part.transform);
    s.models().insert(good, data, lib);
    const NodeId a = s.addNode(makeNodeData(good, "Speaker"));
    const NodeId b = s.addNode(makeNodeData(good, "Speaker 2"));
    std::vector<MeshInstance> out;
    s.collectRenderables(lib, out);
    REQUIRE(out.size() == 2);
    CHECK(out[0].mesh == out[1].mesh);  // one file, one mesh
    CHECK(out[0].material.albedo.z == doctest::Approx(0.3f));
    CHECK(s.localBounds(a, lib).max.y == doctest::Approx(1.0f));
    (void)b;

    ModelContent missing;
    missing.path = "/definitely/not/here.obj";
    const NodeId m = s.addNode(makeNodeData(missing, "Missing"));
    const auto& parts = s.renderParts(m, lib);
    REQUIRE(parts.size() == 1);
    CHECK(parts[0].material.emissive.x > 0.0f);
    REQUIRE(s.models().find(missing));
    CHECK_FALSE(s.models().find(missing)->ok());
}
