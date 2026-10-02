#include "stage/NodeFactory.h"
#include "stage/PathUtil.h"
#include "stage/ProjectFile.h"
#include "stage/SceneJson.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace dmxviz;
using namespace dmxviz::stage;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    explicit TempDir(const char* name) {
        path = fs::temp_directory_path() / name;
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

std::string readText(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeText(const fs::path& p, const std::string& text) {
    std::ofstream out(p, std::ios::binary);
    out << text;
}

std::string describe(const Scene& s, NodeId parent = kInvalidNode) {
    std::string out;
    for (NodeId id : s.childrenOf(parent)) {
        const Node* n = s.find(id);
        out += std::to_string(id) + ":" + std::string(nodeKindName(n->kind())) + ":" + n->name();
        if (!n->children().empty()) out += "(" + describe(s, id) + ")";
        out += ";";
    }
    return out;
}

Transform pose(glm::vec3 p, glm::vec3 eulerDeg, glm::vec3 scale = glm::vec3(1.0f)) {
    Transform t;
    t.position = p;
    t.setEulerDegrees(eulerDeg);
    t.scale = scale;
    return t;
}

// A scene with every node kind and the awkward cases (nesting, overrides,
// deleted ids, non-trivial floats).
Project makeProject(const fs::path& dir) {
    Project p;
    Scene& s = p.scene;
    s.setLayers({Layer{"Default"}, Layer{"Truss", true, true, glm::vec3(1, 0.5f, 0)}});
    const NodeId rig = s.addNode(factory::group("Rig"), pose({0.1f, 0, 0}, {0, 15, 0}));
    NodeData truss = factory::trussStraight(*findTrussProfile("F34"), 7.3f);
    truss.layer = 1;
    truss.as<TrussContent>()->straight.segments = {3.0f, 3.0f, 1.3f};
    const NodeId t = s.addNode(truss, pose({0, 6.25f, -1.1f}, {0, 0, 0}), rig);
    s.addNode(factory::trussCorner(*findTrussProfile("F44"), TrussCornerPreset::FiveWay), {}, rig);
    s.addNode(factory::trussCircle(*findTrussProfile("F33"), 2.5f, 6), pose({0, 8, 0}, {0, 0, 0}), rig);
    s.addNode(factory::trussTower(*findTrussProfile("F34"), 5.5f, 4.25f), pose({4, 0, 0}, {0, 0, 0}));
    s.addNode(factory::fixture("acme/spot-700", "Mode 2", {3, 101}, 12), pose({1, -0.5f, 0}, {0, 0, 0}), t);
    NodeData fx = factory::fixture("generic/rgb-par", "RGB", {1, 1}, 1);
    fx.as<FixtureContent>()->invertPan = true;
    fx.as<FixtureContent>()->panOffsetDeg = -12.5f;
    s.addNode(fx, pose({-1, -0.5f, 0}, {0, 180, 0}), t);
    s.addNode(factory::stageDeck(4, 3, 1.2f), pose({0, 0, 2}, {0, 0, 0}));
    s.addNode(factory::riser(1, 1, 0.4f), pose({0, 1.2f, 2}, {0, 0, 0}));
    s.addNode(factory::steps(1.0f, 1.2f), pose({0, 0, 4.1f}, {0, 0, 0}));
    s.addNode(factory::wall(8, 4), pose({0, 0, -5}, {0, 0, 0}));
    s.addNode(factory::flat(1.2f, 2.4f), pose({3, 0, -4}, {0, -30, 0}));
    s.addNode(factory::floor(), {});
    s.addNode(factory::referenceFigure(1.75f), pose({2, 0, 3}, {0, 0, 0}));
    s.addNode(factory::cameraPreset("FOH", 42.0f), Transform::lookAt({0, 3, 15}, {0, 2, 0}));
    NodeData box = factory::primitive(PrimitiveShape::Cone, {0.3f, 0.7f, 0.3f});
    box.materialOverride = Material{glm::vec3(0.1f, 0.2f, 0.3f), 0.25f, 0.75f, glm::vec3(0, 0.5f, 0)};
    box.visible = false;
    box.color = {0.2f, 0.9f, 0.4f};
    s.addNode(box, pose({1, 2, 3}, {10, 20, 30}, {1, 2, 0.5f}));
    s.addNode(factory::model(pathToUtf8(dir / "models" / "speaker.glb"), true, 0.001f), pose({5, 0, 1}, {0, 0, 0}));
    const NodeId doomed = s.addNode(factory::group("Deleted"));
    s.removeNode(doomed);  // nextNodeId must survive the round trip

    p.environment = {{"hazeDensity", 0.35}, {"exposure", 1.25}};
    p.dmx = {{"interfaces", nlohmann::json::array({{{"type", "sACN"}, {"priority", 100}}})}};
    p.fixtureTypes = {{"acme/spot-700", pathToUtf8(dir / "fixtures" / "spot.dmxviz-fixture.json")},
                      {"generic/rgb-par", ""}};
    return p;
}

}  // namespace

TEST_CASE("project: save, load and save again is byte-identical") {
    TempDir dir("dmxviz_project_test");
    const Project original = makeProject(dir.path);
    const fs::path file = dir.path / "show.dmxviz";
    std::string error;
    REQUIRE_MESSAGE(saveProject(original, file, &error), error);
    const std::string first = readText(file);

    std::vector<std::string> warnings;
    auto loaded = loadProject(file, &error, &warnings);
    REQUIRE_MESSAGE(loaded, error);
    CHECK(warnings.empty());
    CHECK(describe(loaded->scene) == describe(original.scene));
    CHECK(loaded->scene.nextId() == original.scene.nextId());
    CHECK(loaded->scene.layers().size() == 2);
    CHECK(loaded->scene.layers()[1].locked);
    for (NodeId id : original.scene.depthFirst()) {
        REQUIRE(loaded->scene.contains(id));
        CHECK(loaded->scene.find(id)->transform() == original.scene.find(id)->transform());
        CHECK(nodeToJson(loaded->scene.snapshot(id)) == nodeToJson(original.scene.snapshot(id)));
    }
    CHECK(loaded->environment == original.environment);
    CHECK(loaded->dmx == original.dmx);
    REQUIRE(loaded->fixtureTypes.size() == 2);
    CHECK(loaded->fixtureTypes[0].path == original.fixtureTypes[0].path);
    CHECK(loaded->fixtureTypes[1].path.empty());

    // Paths are stored relative to the project and come back absolute.
    const NodeId model = loaded->scene.nodesOfKind(NodeKind::Model).front();
    CHECK(loaded->scene.find(model)->as<ModelContent>()->path == pathToUtf8(dir.path / "models" / "speaker.glb"));
    CHECK(first.find("\"models/speaker.glb\"") != std::string::npos);
    CHECK(first.find("\"fixtures/spot.dmxviz-fixture.json\"") != std::string::npos);
    // Floats are written in their shortest form and patch data is present.
    CHECK(first.find("6.25") != std::string::npos);
    CHECK(first.find("0.10000000149") == std::string::npos);
    CHECK(first.find("\"universe\": 3") != std::string::npos);

    REQUIRE(saveProject(*loaded, file, &error));
    CHECK(readText(file) == first);
}

TEST_CASE("project: forward compatibility keeps unknown data and warns") {
    TempDir dir("dmxviz_project_compat");
    std::string error;
    std::vector<std::string> warnings;
    auto p = loadProject(fs::path(DMXVIZ_TEST_DATA_DIR) / "stage" / "forward_compat.dmxviz", &error, &warnings);
    REQUIRE_MESSAGE(p, error);
    // Newer format, unknown kind, unknown enum value, duplicate id.
    CHECK(warnings.size() == 4);
    const Scene& s = p->scene;
    CHECK(s.nodeCount() == 5);
    const Node* screen = s.find(5);
    REQUIRE(screen);
    CHECK(screen->kind() == NodeKind::Unknown);
    CHECK(screen->as<UnknownContent>()->kind == "ledScreen");
    CHECK(screen->transform().eulerDegrees().y == doctest::Approx(90.0f));  // rotationDeg accepted
    CHECK(s.childrenOf(5).size() == 1);
    CHECK_FALSE(s.effectiveVisible(6));  // layer "Video" is hidden

    const Node* truss = s.find(7);
    REQUIRE(truss);
    CHECK(truss->as<TrussContent>()->profile.width == doctest::Approx(0.39f));  // catalogue defaults
    CHECK(truss->as<TrussContent>()->piece == TrussPiece::Straight);         // unknown "spiral" -> default
    CHECK(truss->as<TrussContent>()->straight.length == doctest::Approx(6.0f));
    const NodeId dup = s.findByName("Duplicate id");
    CHECK(dup != 7);
    CHECK(dup >= 100);
    const Node* fx = s.find(s.findByName("No id"));
    REQUIRE(fx);
    CHECK(fx->as<FixtureContent>()->patch == DmxPatch{2, 101});
    CHECK(s.nextId() > 100);
    CHECK(p->environment["hazeDensity"].get<double>() == doctest::Approx(0.4));

    REQUIRE(saveProject(*p, dir.path / "again.dmxviz", &error));
    const std::string text = readText(dir.path / "again.dmxviz");
    CHECK(text.find("\"ledWalls\"") != std::string::npos);
    CHECK(text.find("\"pixelPitch\": 3.9") != std::string::npos);
    CHECK(text.find("\"kind\": \"ledScreen\"") != std::string::npos);
    CHECK(text.find("\"formatVersion\": 1") != std::string::npos);
}

TEST_CASE("project: clear error messages") {
    TempDir dir("dmxviz_project_errors");
    std::string error;
    CHECK_FALSE(loadProject(dir.path / "missing.dmxviz", &error));
    CHECK(error.find("cannot open") != std::string::npos);

    writeText(dir.path / "bad.dmxviz", "{ \"formatVersion\": 1, ");
    CHECK_FALSE(loadProject(dir.path / "bad.dmxviz", &error));
    CHECK(error.find("invalid JSON") != std::string::npos);

    writeText(dir.path / "nover.dmxviz", "{ \"scene\": {} }");
    CHECK_FALSE(loadProject(dir.path / "nover.dmxviz", &error));
    CHECK(error.find("formatVersion") != std::string::npos);

    writeText(dir.path / "type.dmxviz",
              R"({ "formatVersion": 1, "scene": { "nodes": [ { "kind": "group", "children": [
                   { "kind": "truss", "truss": { "straight": { "length": "long" } } } ] } ] } })");
    CHECK_FALSE(loadProject(dir.path / "type.dmxviz", &error));
    CHECK(error.find("scene.nodes[0].children[0].truss.straight.length: expected a number") != std::string::npos);

    writeText(dir.path / "range.dmxviz",
              R"({ "formatVersion": 1, "scene": { "nodes": [ { "kind": "fixture", "fixture": { "patch": { "address": 600 } } } ] } })");
    CHECK_FALSE(loadProject(dir.path / "range.dmxviz", &error));
    CHECK(error.find("address") != std::string::npos);

    CHECK_FALSE(saveProject(Project{}, dir.path / "no" / "such" / "dir" / "x.dmxviz", &error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("clipboard text round-trips node subtrees") {
    Scene s;
    const NodeId g = s.addNode(factory::group("Booms"), pose({1, 0, 0}, {0, 45, 0}));
    s.addNode(factory::fixture("generic/par", "3ch", {1, 10}), {}, g);
    const std::string text = snapshotsToClipboardText({s.snapshot(g)});
    std::string error;
    auto nodes = snapshotsFromClipboardText(text, &error);
    REQUIRE_MESSAGE(nodes, error);
    REQUIRE(nodes->size() == 1);
    CHECK(nodes->front().children.size() == 1);
    CHECK(nodes->front().transform == s.find(g)->transform());
    CHECK_FALSE(snapshotsFromClipboardText("hello", &error));
    CHECK_FALSE(snapshotsFromClipboardText("{\"nodes\": []}", &error));
}

TEST_CASE("jsonFloat writes the shortest exact form") {
    CHECK(jsonFloat(0.1f) == 0.1);
    CHECK(static_cast<float>(jsonFloat(0.1f)) == 0.1f);
    CHECK(static_cast<float>(jsonFloat(1.0f / 3.0f)) == 1.0f / 3.0f);
    CHECK(jsonFloat(std::numeric_limits<float>::infinity()) == 0.0);
}
