#include "stage/NodeFactory.h"

#include <format>

namespace dmxviz::stage::factory {
namespace {

std::string metres(float v) { return std::format("{:g} m", v); }

}  // namespace

NodeData group(std::string name) { return makeNodeData(GroupContent{}, std::move(name)); }

NodeData primitive(PrimitiveShape shape, const glm::vec3& size) {
    PrimitiveContent p;
    p.shape = shape;
    p.size = size;
    std::string name(primitiveShapeName(shape));
    if (!name.empty()) name[0] = static_cast<char>(name[0] - 'a' + 'A');
    return makeNodeData(p, name);
}

NodeData floor(float width, float depth) {
    PrimitiveContent p;
    p.shape = PrimitiveShape::Plane;
    p.size = {width, 1.0f, depth};
    p.material = Material{glm::vec3(0.05f), 0.9f, 0.0f, glm::vec3(0.0f)};
    NodeData d = makeNodeData(p, "Floor");
    d.locked = true;  // the floor is rarely meant to be dragged around
    return d;
}

NodeData model(std::string path, bool zUp, float unitScale) {
    ModelContent m;
    m.path = std::move(path);
    m.zUp = zUp;
    m.unitScale = unitScale;
    const std::size_t slash = m.path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? m.path : m.path.substr(slash + 1);
    return makeNodeData(m, name.empty() ? "Model" : name);
}

NodeData trussStraight(const TrussProfile& profile, float length) {
    TrussContent t;
    t.profile = profile;
    t.piece = TrussPiece::Straight;
    t.straight.length = length;
    return makeNodeData(t, std::format("Truss {} {}", profile.name, metres(length)));
}

NodeData trussCorner(const TrussProfile& profile, TrussCornerPreset preset) {
    TrussContent t;
    t.profile = profile;
    t.piece = TrussPiece::Corner;
    t.corner.faces = trussCornerFaces(preset);
    return makeNodeData(t, std::format("Corner {}", profile.name));
}

NodeData trussArc(const TrussProfile& profile, float radius, float angleDeg, int pieces) {
    TrussContent t;
    t.profile = profile;
    t.piece = TrussPiece::Arc;
    t.arc.radius = radius;
    t.arc.angleDeg = angleDeg;
    t.arc.pieces = pieces;
    return makeNodeData(t, std::format("Arc {} r={}", profile.name, metres(radius)));
}

NodeData trussCircle(const TrussProfile& profile, float radius, int pieces) {
    NodeData d = trussArc(profile, radius, 360.0f, pieces);
    d.name = std::format("Circle {} r={}", profile.name, metres(radius));
    return d;
}

NodeData trussTower(const TrussProfile& profile, float height, float sleeveHeight) {
    TrussContent t;
    t.profile = profile;
    t.piece = TrussPiece::Tower;
    t.tower.height = height;
    t.tower.sleeveHeight = sleeveHeight;
    return makeNodeData(t, std::format("Tower {} {}", profile.name, metres(height)));
}

NodeData stageDeck(int columns, int rows, float height, const glm::vec2& panelSize) {
    StageDeckContent d;
    d.columns = columns;
    d.rows = rows;
    d.height = height;
    d.panelSize = panelSize;
    return makeNodeData(d, std::format("Stage {}x{} m", panelSize.x * static_cast<float>(columns),
                                       panelSize.y * static_cast<float>(rows)));
}

NodeData riser(int columns, int rows, float height, const glm::vec2& panelSize) {
    NodeData d = stageDeck(columns, rows, height, panelSize);
    d.as<StageDeckContent>()->style = DeckStyle::Riser;
    d.as<StageDeckContent>()->legs = false;
    d.name = "Riser";
    return d;
}

NodeData steps(float width, float height) {
    StepsContent s;
    s.width = width;
    s.height = height;
    return makeNodeData(s, "Steps");
}

NodeData wall(float width, float height, float thickness) {
    WallContent w;
    w.width = width;
    w.height = height;
    w.thickness = thickness;
    return makeNodeData(w, "Wall");
}

NodeData flat(float width, float height) {
    WallContent w;
    w.style = WallStyle::Flat;
    w.width = width;
    w.height = height;
    w.thickness = 0.08f;
    return makeNodeData(w, "Flat");
}

NodeData fixture(std::string fixtureTypeId, std::string modeName, DmxPatch patch, int fixtureNumber) {
    FixtureContent f;
    f.fixtureTypeId = std::move(fixtureTypeId);
    f.modeName = std::move(modeName);
    f.patch = patch;
    f.fixtureNumber = fixtureNumber;
    const std::size_t slash = f.fixtureTypeId.find_last_of('/');
    std::string name = slash == std::string::npos ? f.fixtureTypeId : f.fixtureTypeId.substr(slash + 1);
    if (fixtureNumber > 0) name += std::format(" {}", fixtureNumber);
    return makeNodeData(f, name.empty() ? "Fixture" : name);
}

NodeData cameraPreset(std::string name, float fovYDeg) {
    CameraPresetContent c;
    c.fovYDeg = fovYDeg;
    return makeNodeData(c, std::move(name));
}

NodeData referenceFigure(float height) {
    ReferenceFigureContent f;
    f.height = height;
    return makeNodeData(f, "Person");
}

}  // namespace dmxviz::stage::factory
