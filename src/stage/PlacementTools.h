#pragma once
// Stage builder placement tools: snapping, arrays, align/distribute, mirror,
// drop to floor and hang-on-truss (FR-STG-5, FR-STG-6).
//
// Pure helpers return values; the tools that change the scene return a
// ready-to-execute Command (nullptr when there is nothing to do), e.g.
//   stack.execute(tools::linearArray(scene, selection.ids(), 7, {0.5f, 0, 0}));
// Unless stated otherwise, offsets, planes and directions are in world space.

#include "assets/AssetLibrary.h"
#include "stage/Command.h"
#include "stage/Picking.h"
#include "stage/Scene.h"

#include <memory>
#include <optional>
#include <vector>

namespace dmxviz::stage::tools {

// ---- snapping ----------------------------------------------------------------

struct SnapSettings {
    bool grid = true;
    float gridStep = 0.25f;  // metres
    bool angle = true;
    float angleStepDeg = 15.0f;
};

float snapValue(float value, float step);              // step <= 0 returns value
glm::vec3 snapToGrid(const glm::vec3& p, float step);
float snapAngleDeg(float degrees, float stepDeg);
// Snaps each Euler angle (Transform::eulerDegrees order) to the step.
glm::quat snapRotation(const glm::quat& q, float stepDeg);
// Applies the enabled snaps to a gizmo result (position in the transform's own space).
Transform applySnap(const Transform& t, const SnapSettings& settings);

// Vertex snap: the corner of the picked triangle closest to the hit point.
glm::vec3 nearestVertex(const PickHit& hit);
// Face snap: world pose that puts an object's origin on the hit point. With
// alignToNormal its local +Y follows the surface normal (keeping its heading
// as far as possible); otherwise only the position changes.
glm::mat4 placeOnSurface(const PickHit& hit, const glm::mat4& currentWorld, bool alignToNormal);

// ---- bounds ------------------------------------------------------------------

// Union of the world bounds of the nodes (and their subtrees).
Aabb selectionBounds(const Scene& scene, assets::AssetLibrary& library, const std::vector<NodeId>& ids);
// The node's local transform after moving it by `worldDelta` (rotation and
// scale stay bit-exact).
Transform translatedInWorld(const Scene& scene, NodeId id, const glm::vec3& worldDelta);

// ---- arrays ------------------------------------------------------------------

// `copies` new copies of the nodes, copy k moved by k * offset.
std::unique_ptr<Command> linearArray(const Scene& scene, const std::vector<NodeId>& ids, int copies,
                                     const glm::vec3& offset);
// A counts.x * counts.y * counts.z grid that includes the originals at cell (0, 0, 0).
std::unique_ptr<Command> gridArray(const Scene& scene, const std::vector<NodeId>& ids, const glm::ivec3& counts,
                                   const glm::vec3& spacing);

struct CircularArraySettings {
    glm::vec3 centre{0.0f};
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    int count = 8;                  // items including the original
    float totalAngleDeg = 360.0f;   // 360 = full circle (items evenly spaced, none doubled); less = arc from first to last
    bool rotateCopies = true;       // turn copies with the circle (fixtures keep facing the centre)
};
std::unique_ptr<Command> circularArray(const Scene& scene, const std::vector<NodeId>& ids,
                                       const CircularArraySettings& settings);

// ---- align / distribute / mirror ---------------------------------------------

enum class Axis { X, Y, Z };
enum class AlignMode { Min, Centre, Max };

// Moves nodes so their bounds' min/centre/max on `axis` match `target`, or
// the combined bounds of all of them when no target is given.
std::unique_ptr<Command> alignNodes(const Scene& scene, assets::AssetLibrary& library, const std::vector<NodeId>& ids,
                                    Axis axis, AlignMode mode, std::optional<float> target = std::nullopt);

enum class DistributeMode {
    Centres,  // equal distance between centres
    Gaps,     // equal free space between neighbouring bounds
};
// Spreads 3+ nodes evenly between the outermost two (which stay put).
std::unique_ptr<Command> distributeNodes(const Scene& scene, assets::AssetLibrary& library,
                                         const std::vector<NodeId>& ids, Axis axis, DistributeMode mode);

// Mirrors positions and orientations across the world plane `axis = position`
// (geometry itself is not inverted, so fixtures stay valid). With copy, the
// originals stay and mirrored copies are added.
std::unique_ptr<Command> mirrorNodes(const Scene& scene, const std::vector<NodeId>& ids, Axis axis, float position,
                                     bool copy);

// Lowers (or raises) nodes until their bounds rest on the highest surface
// below them among `obstacles` (other nodes' instances; the nodes' own
// instances are ignored), or on y = floorY when nothing is below.
std::unique_ptr<Command> dropToFloor(const Scene& scene, assets::AssetLibrary& library,
                                     const std::vector<NodeId>& ids, float floorY = 0.0f,
                                     const std::vector<MeshInstance>* obstacles = nullptr);

// ---- hang on truss -------------------------------------------------------------

struct HangOptions {
    glm::vec3 facing{0.0f, 0.0f, 1.0f};  // world direction for the fixture's local +Z (front), toward the audience by default
    float clampDrop = 0.06f;             // from the underside of the chord to the fixture origin (clamp + bracket)
    float snapAlong = 0.0f;              // > 0: snap the position along the chord to this step
    bool reparent = false;               // make the fixture a child of the truss (moves with it)
};

struct HangResult {
    Transform local;          // for the fixture under its (new) parent
    NodeId parent = kInvalidNode;
    glm::mat4 world{1.0f};
    glm::vec3 chordPoint{0.0f};  // closest point on the chord centre line (world)
    glm::vec3 chordDirection{1.0f, 0.0f, 0.0f};
};

// Clamps a fixture to the bottom chord of a truss nearest to `hitPoint`
// (usually the picked point on the truss): the fixture hangs straight down
// (identity pitch/roll, see the fixture convention in ARCHITECTURE.md) below
// the chord and is turned about Y so local +Z faces options.facing. Vertical
// truss (towers) is clamped on the outside of the nearest chord instead.
// Returns nullopt if `truss` is not a truss node or `fixture` does not exist.
std::optional<HangResult> hangOnTruss(const Scene& scene, NodeId fixture, NodeId truss, const glm::vec3& hitPoint,
                                      const HangOptions& options = {});
std::unique_ptr<Command> hangOnTrussCommand(const Scene& scene, NodeId fixture, NodeId truss,
                                            const glm::vec3& hitPoint, const HangOptions& options = {});

// Hangs `fixtures` on `truss` spread evenly along it: the fixtures keep their current order along the truss
// and each takes the middle of an equal share of the chord's length. The length follows the chord, so arcs and
// circles are spread by arc length (equal angles), not along their bounding box. Returns nullptr if `truss` is
// not a truss node or no fixture can be hung.
std::unique_ptr<Command> hangSpreadOnTrussCommand(const Scene& scene, const std::vector<NodeId>& fixtures,
                                                  NodeId truss, const HangOptions& options = {});

}  // namespace dmxviz::stage::tools
