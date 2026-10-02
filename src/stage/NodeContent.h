#pragma once
// Kind-specific data of scene nodes.
//
// Design: every node in the scene graph is the same `Node` class (id, name,
// hierarchy, transform, visibility...). What makes a node a truss, a deck or
// a fixture is its *content*: one plain struct per kind, held in the
// `NodeContent` variant below. Plain value structs are easy to copy (undo
// snapshots, duplicate, clipboard), compare and serialise.
//
// Adding a new node kind:
//   1. add a `XxxContent` struct here and append it to `NodeContent` and `NodeKind`
//      (same position in both, the static_assert in NodeContent.cpp checks it);
//   2. give it a name in nodeKindName() and JSON (de)serialisation in SceneJson.cpp;
//   3. if it has geometry, emit its parts in NodeGeometry.cpp.
//
// Units: metres. Angles that the user edits are kept in degrees (the field name
// ends in `Deg`) so that project files round-trip them exactly.

#include "core/SceneTypes.h"
#include "stage/TrussProfile.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace dmxviz::stage {

// Default surface materials (linear RGB).
namespace materials {
Material grey();         // neutral default for primitives
Material aluminium();    // truss, deck legs
Material stageTop();     // black-grey painted deck surface
Material blackFabric();  // skirts, masking
Material wallPaint();    // light grey flat / wall
Material figure();       // reference person
}  // namespace materials

// ---------------------------------------------------------------------------
struct GroupContent {};

// ---------------------------------------------------------------------------
enum class PrimitiveShape { Box, Cylinder, Sphere, Plane, Cone, Disc };

// A built-in primitive scaled to `size`. Box/sphere/cylinder are centred on the
// origin; cone and disc have their base at y = 0; plane and disc lie in XZ.
// size.x / size.z are the diameters for round shapes, size.y the height
// (ignored for plane and disc).
struct PrimitiveContent {
    PrimitiveShape shape = PrimitiveShape::Box;
    glm::vec3 size{1.0f};
    Material material = materials::grey();
};

// ---------------------------------------------------------------------------
// An imported 3D model file (glTF/GLB, OBJ, 3DS). The loaded meshes live in the
// AssetLibrary and are owned by the Scene's model cache; the node only stores
// how to find and convert the file.
struct ModelContent {
    std::string path;        // absolute in memory; stored relative to the project file
    bool zUp = false;        // source is Z-up (3ds Max, many CAD exports)
    float unitScale = 1.0f;  // 0.001 for millimetre files
};

// ---------------------------------------------------------------------------
enum class TrussPiece { Straight, Corner, Arc, Tower };

// Bit flags for the six faces of a box corner block that connect to other truss.
enum TrussFace : std::uint8_t {
    kTrussFacePosX = 1,
    kTrussFaceNegX = 2,
    kTrussFacePosY = 4,
    kTrussFaceNegY = 8,
    kTrussFacePosZ = 16,
    kTrussFaceNegZ = 32,
};

// Common corner block configurations.
enum class TrussCornerPreset {
    TwoWay,         // 90 deg L corner (+X, +Z)
    ThreeWayT,      // T junction in a plane (+X, -X, +Z)
    ThreeWayCorner, // L corner with a leg down (+X, +Z, -Y)
    FourWayCross,   // cross in a plane (+-X, +-Z)
    FourWayT,       // T with a leg down (+-X, +Z, -Y)
    FiveWay,        // cross with a leg down
    SixWay,         // all faces
};
std::uint8_t trussCornerFaces(TrussCornerPreset preset);

// A straight run, centred on the origin along local X. Without explicit
// segments it is split into standard lengths (truss::splitRun); with them,
// the segments define the run and `length` should be kept equal to their sum.
struct TrussStraight {
    float length = 3.0f;
    std::vector<float> segments;
};

struct TrussCorner {
    std::uint8_t faces = kTrussFacePosX | kTrussFacePosZ;
};

// Horizontal arc in the XZ plane around the node origin, starting on +X and
// turning toward -Z (counter-clockwise seen from above). 360 deg = full circle.
struct TrussArc {
    float radius = 2.0f;      // of the truss centre line
    float angleDeg = 90.0f;
    int pieces = 1;           // number of identical arc segments the arc is built from
};

// Ground support tower: base plate on the floor (origin), vertical truss, a
// sleeve block that carries the horizontal truss and a top section.
struct TrussTower {
    float height = 5.0f;         // floor to top of the top section
    float sleeveHeight = 4.0f;   // floor to the centre of the sleeve block
    float basePlateSize = 0.8f;  // square base plate edge length
};

// A generated truss piece. Only the parameters of the active `piece` are used;
// the others are kept so switching piece type in the UI does not lose values.
struct TrussContent {
    TrussProfile profile;
    TrussPiece piece = TrussPiece::Straight;
    TrussStraight straight;
    TrussCorner corner;
    TrussArc arc;
    TrussTower tower;
};

// ---------------------------------------------------------------------------
enum class DeckStyle {
    Deck,   // panels on legs, optional skirt
    Riser,  // closed box (fascia on all sides), e.g. drum riser
};

// A platform built from a grid of deck panels. The origin is on the floor at
// the centre of the footprint; the top surface is at y = height.
struct StageDeckContent {
    DeckStyle style = DeckStyle::Deck;
    int columns = 1;                  // panels along X
    int rows = 1;                     // panels along Z
    glm::vec2 panelSize{2.0f, 1.0f};  // panel width (X) and depth (Z)
    float height = 0.6f;
    float thickness = 0.1f;           // panel including frame
    bool legs = true;
    bool skirt = false;
    Material surface = materials::stageTop();
    Material skirtMaterial = materials::blackFabric();
};

// A flight of steps rising toward -Z (upstage). The origin is on the floor at
// the centre of the footprint.
struct StepsContent {
    float width = 1.0f;
    float height = 0.6f;     // top tread height
    int steps = 0;           // 0 = choose from height (about 0.2 m rise)
    float treadDepth = 0.3f;
    bool handrails = false;
    Material material = materials::stageTop();

    int effectiveSteps() const;
};

// ---------------------------------------------------------------------------
enum class WallStyle {
    Wall,  // solid block
    Flat,  // theatre flat: thin skin on a timber frame with a brace
};

// A wall or flat standing on the floor; the origin is at its bottom centre and
// the face looks toward +Z.
struct WallContent {
    WallStyle style = WallStyle::Wall;
    float width = 4.0f;
    float height = 3.0f;
    float thickness = 0.1f;
    Material material = materials::wallPaint();
};

// ---------------------------------------------------------------------------
// DMX address of a fixture. universe is the logical, 1-based universe; 0 means
// "not patched". address is 1..512.
struct DmxPatch {
    std::uint32_t universe = 0;
    std::uint32_t address = 0;

    bool patched() const { return universe > 0 && address > 0; }
    bool operator==(const DmxPatch&) const = default;
};

// A lighting fixture. The stage only stores what the user configured; the
// simulation resolves fixtureTypeId/modeName in the fixture library and emits
// the fixture's meshes and beams with this node's world matrix and id.
struct FixtureContent {
    std::string fixtureTypeId;  // "manufacturer/model" library id
    std::string modeName;
    DmxPatch patch;
    int fixtureNumber = 0;  // user-facing fixture id ("FID"), 0 = none
    bool invertPan = false;
    bool invertTilt = false;
    float panOffsetDeg = 0.0f;
    float tiltOffsetDeg = 0.0f;
};

// ---------------------------------------------------------------------------
// A saved viewpoint. The node transform is the camera pose: it looks along
// local -Z with local +Y up (see Transform::lookAt).
struct CameraPresetContent {
    float fovYDeg = 50.0f;          // vertical field of view
    float targetDistance = 10.0f;   // orbit pivot distance in front of the camera
};

// ---------------------------------------------------------------------------
// A neutral person silhouette standing on the origin, facing +Z, for scale.
struct ReferenceFigureContent {
    float height = 1.8f;
    Material material = materials::figure();
};

// ---------------------------------------------------------------------------
// A node kind written by a newer DmxViz. Kept verbatim so that loading and
// saving such a project does not lose data. Renders nothing.
struct UnknownContent {
    std::string kind;
    nlohmann::json data = nlohmann::json::object();
};

// ---------------------------------------------------------------------------
enum class NodeKind {
    Group,
    Primitive,
    Model,
    Truss,
    StageDeck,
    Steps,
    Wall,
    Fixture,
    CameraPreset,
    ReferenceFigure,
    Unknown,
};

using NodeContent = std::variant<GroupContent, PrimitiveContent, ModelContent, TrussContent, StageDeckContent,
                                 StepsContent, WallContent, FixtureContent, CameraPresetContent,
                                 ReferenceFigureContent, UnknownContent>;

inline NodeKind kindOf(const NodeContent& c) { return static_cast<NodeKind>(c.index()); }

// Stable names used in project files and the UI ("truss", "stageDeck", ...).
std::string_view nodeKindName(NodeKind kind);
bool parseNodeKind(std::string_view name, NodeKind& out);  // false for unknown names
// Human readable default name for new nodes ("Truss", "Stage deck").
std::string_view nodeKindLabel(NodeKind kind);
NodeContent defaultContent(NodeKind kind);

std::string_view primitiveShapeName(PrimitiveShape shape);
bool parsePrimitiveShape(std::string_view name, PrimitiveShape& out);
std::string_view trussPieceName(TrussPiece piece);
bool parseTrussPiece(std::string_view name, TrussPiece& out);

}  // namespace dmxviz::stage
