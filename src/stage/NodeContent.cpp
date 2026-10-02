#include "stage/NodeContent.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace dmxviz::stage {

static_assert(std::variant_size_v<NodeContent> == static_cast<std::size_t>(NodeKind::Unknown) + 1,
              "NodeContent alternatives and NodeKind values must match one to one");
static_assert(std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(NodeKind::Fixture), NodeContent>,
                             FixtureContent>);
static_assert(std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(NodeKind::Unknown), NodeContent>,
                             UnknownContent>);

namespace materials {

Material grey() { return Material{glm::vec3(0.5f), 0.6f, 0.0f, glm::vec3(0.0f)}; }
Material aluminium() { return Material{glm::vec3(0.8f, 0.81f, 0.82f), 0.35f, 1.0f, glm::vec3(0.0f)}; }
Material stageTop() { return Material{glm::vec3(0.035f), 0.8f, 0.0f, glm::vec3(0.0f)}; }
Material blackFabric() { return Material{glm::vec3(0.01f), 0.95f, 0.0f, glm::vec3(0.0f)}; }
Material wallPaint() { return Material{glm::vec3(0.6f), 0.85f, 0.0f, glm::vec3(0.0f)}; }
Material figure() { return Material{glm::vec3(0.25f, 0.4f, 0.65f), 0.7f, 0.0f, glm::vec3(0.0f)}; }

}  // namespace materials

std::uint8_t trussCornerFaces(TrussCornerPreset preset) {
    switch (preset) {
        case TrussCornerPreset::TwoWay: return kTrussFacePosX | kTrussFacePosZ;
        case TrussCornerPreset::ThreeWayT: return kTrussFacePosX | kTrussFaceNegX | kTrussFacePosZ;
        case TrussCornerPreset::ThreeWayCorner: return kTrussFacePosX | kTrussFacePosZ | kTrussFaceNegY;
        case TrussCornerPreset::FourWayCross: return kTrussFacePosX | kTrussFaceNegX | kTrussFacePosZ | kTrussFaceNegZ;
        case TrussCornerPreset::FourWayT: return kTrussFacePosX | kTrussFaceNegX | kTrussFacePosZ | kTrussFaceNegY;
        case TrussCornerPreset::FiveWay:
            return kTrussFacePosX | kTrussFaceNegX | kTrussFacePosZ | kTrussFaceNegZ | kTrussFaceNegY;
        case TrussCornerPreset::SixWay: return 63;
    }
    return kTrussFacePosX | kTrussFacePosZ;
}

int StepsContent::effectiveSteps() const {
    if (steps > 0) return steps;
    return std::max(1, static_cast<int>(std::lround(height / 0.2f)));
}

namespace {

struct KindInfo {
    NodeKind kind;
    std::string_view name;
    std::string_view label;
};

constexpr std::array<KindInfo, 11> kKinds = {{
    {NodeKind::Group, "group", "Group"},
    {NodeKind::Primitive, "primitive", "Primitive"},
    {NodeKind::Model, "model", "Model"},
    {NodeKind::Truss, "truss", "Truss"},
    {NodeKind::StageDeck, "stageDeck", "Stage deck"},
    {NodeKind::Steps, "steps", "Steps"},
    {NodeKind::Wall, "wall", "Wall"},
    {NodeKind::Fixture, "fixture", "Fixture"},
    {NodeKind::CameraPreset, "camera", "Camera"},
    {NodeKind::ReferenceFigure, "referenceFigure", "Person"},
    {NodeKind::Unknown, "unknown", "Unknown"},
}};

}  // namespace

std::string_view nodeKindName(NodeKind kind) { return kKinds[static_cast<std::size_t>(kind)].name; }
std::string_view nodeKindLabel(NodeKind kind) { return kKinds[static_cast<std::size_t>(kind)].label; }

bool parseNodeKind(std::string_view name, NodeKind& out) {
    for (const KindInfo& k : kKinds) {
        if (k.kind != NodeKind::Unknown && k.name == name) {
            out = k.kind;
            return true;
        }
    }
    return false;
}

NodeContent defaultContent(NodeKind kind) {
    switch (kind) {
        case NodeKind::Group: return GroupContent{};
        case NodeKind::Primitive: return PrimitiveContent{};
        case NodeKind::Model: return ModelContent{};
        case NodeKind::Truss: return TrussContent{};
        case NodeKind::StageDeck: return StageDeckContent{};
        case NodeKind::Steps: return StepsContent{};
        case NodeKind::Wall: return WallContent{};
        case NodeKind::Fixture: return FixtureContent{};
        case NodeKind::CameraPreset: return CameraPresetContent{};
        case NodeKind::ReferenceFigure: return ReferenceFigureContent{};
        case NodeKind::Unknown: return UnknownContent{};
    }
    return GroupContent{};
}

std::string_view primitiveShapeName(PrimitiveShape shape) {
    switch (shape) {
        case PrimitiveShape::Box: return "box";
        case PrimitiveShape::Cylinder: return "cylinder";
        case PrimitiveShape::Sphere: return "sphere";
        case PrimitiveShape::Plane: return "plane";
        case PrimitiveShape::Cone: return "cone";
        case PrimitiveShape::Disc: return "disc";
    }
    return "box";
}

bool parsePrimitiveShape(std::string_view name, PrimitiveShape& out) {
    for (PrimitiveShape s : {PrimitiveShape::Box, PrimitiveShape::Cylinder, PrimitiveShape::Sphere,
                             PrimitiveShape::Plane, PrimitiveShape::Cone, PrimitiveShape::Disc}) {
        if (primitiveShapeName(s) == name) {
            out = s;
            return true;
        }
    }
    return false;
}

std::string_view trussPieceName(TrussPiece piece) {
    switch (piece) {
        case TrussPiece::Straight: return "straight";
        case TrussPiece::Corner: return "corner";
        case TrussPiece::Arc: return "arc";
        case TrussPiece::Tower: return "tower";
    }
    return "straight";
}

bool parseTrussPiece(std::string_view name, TrussPiece& out) {
    for (TrussPiece p : {TrussPiece::Straight, TrussPiece::Corner, TrussPiece::Arc, TrussPiece::Tower}) {
        if (trussPieceName(p) == name) {
            out = p;
            return true;
        }
    }
    return false;
}

}  // namespace dmxviz::stage
