#include "stage/PlacementTools.h"

#include "stage/Commands.h"
#include "stage/TrussBuilder.h"

#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_set>

namespace dmxviz::stage::tools {
namespace {

int axisIndex(Axis a) { return static_cast<int>(a); }

float boundValue(const Aabb& b, int axis, AlignMode mode) {
    switch (mode) {
        case AlignMode::Min: return b.min[axis];
        case AlignMode::Centre: return b.center()[axis];
        case AlignMode::Max: return b.max[axis];
    }
    return b.center()[axis];
}

glm::vec3 unitAxis(int axis) {
    glm::vec3 v(0.0f);
    v[axis] = 1.0f;
    return v;
}

// Mirror matrix for one axis (diag with -1).
glm::mat4 reflection(int axis) {
    glm::mat4 s(1.0f);
    s[axis][axis] = -1.0f;
    return s;
}

// Conjugates a local transform by the axis reflection: S * L * S. This is how
// a child moves when its parent's world matrix W becomes Mw * W * S.
Transform mirroredLocal(const Transform& t, int axis) {
    Transform r = t;
    r.position[axis] = -r.position[axis];
    glm::mat3 s(1.0f);
    s[axis][axis] = -1.0f;
    r.rotation = glm::normalize(glm::quat_cast(s * glm::mat3_cast(t.rotation) * s));
    return r;
}

// Keeps the original scale when decomposition only added float noise.
void keepScale(Transform& t, const glm::vec3& original) {
    if (glm::all(glm::lessThan(glm::abs(t.scale - original), glm::vec3(1e-4f)))) t.scale = original;
}

// Builds an InsertNodesCommand with `copies` copies of every top-level node in
// `ids`; placement(source, k) gives copy k's local transform (k = 0..copies-1).
// Copies are inserted right after their original, in order.
std::unique_ptr<Command> makeCopies(const Scene& scene, const std::vector<NodeId>& ids, int copies,
                                    const std::function<Transform(NodeId, int)>& placement, std::string name) {
    const std::vector<NodeId> sorted = scene.sortedByTreeOrder(scene.topLevelOnly(ids));
    if (sorted.empty() || copies <= 0) return nullptr;
    std::vector<NodeInsertion> insertions;
    // Last original first, so inserting never shifts an original handled later.
    for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
        const NodeId source = *it;
        const int index = scene.indexInParent(source);
        for (int k = 0; k < copies; ++k) {
            NodeInsertion ins;
            ins.snapshot = scene.snapshot(source);
            ins.snapshot.clearIds();
            ins.snapshot.transform = placement(source, k);
            ins.parent = scene.find(source)->parent();
            ins.index = index + 1 + k;
            insertions.push_back(std::move(ins));
        }
    }
    return std::make_unique<InsertNodesCommand>(std::move(name), std::move(insertions));
}

// Local transform for `id` placed at world motion * current world.
Transform movedBy(const Scene& scene, NodeId id, const glm::mat4& motion) {
    const Node* n = scene.find(id);
    Transform t = scene.localFromWorld(n->parent(), motion * scene.worldMatrix(id));
    keepScale(t, n->transform().scale);
    return t;
}

}  // namespace

// ---- snapping ----------------------------------------------------------------

float snapValue(float value, float step) { return step > 0.0f ? std::round(value / step) * step : value; }

glm::vec3 snapToGrid(const glm::vec3& p, float step) {
    return {snapValue(p.x, step), snapValue(p.y, step), snapValue(p.z, step)};
}

float snapAngleDeg(float degrees, float stepDeg) { return snapValue(degrees, stepDeg); }

glm::quat snapRotation(const glm::quat& q, float stepDeg) {
    const glm::vec3 e = Transform::eulerDegreesFromQuat(q);
    return Transform::quatFromEulerDegrees(
        {snapAngleDeg(e.x, stepDeg), snapAngleDeg(e.y, stepDeg), snapAngleDeg(e.z, stepDeg)});
}

Transform applySnap(const Transform& t, const SnapSettings& settings) {
    Transform r = t;
    if (settings.grid) r.position = snapToGrid(t.position, settings.gridStep);
    if (settings.angle) r.rotation = snapRotation(t.rotation, settings.angleStepDeg);
    return r;
}

glm::vec3 nearestVertex(const PickHit& hit) {
    glm::vec3 best = hit.triangleWorld[0];
    for (const glm::vec3& v : hit.triangleWorld) {
        if (glm::distance(v, hit.point) < glm::distance(best, hit.point)) best = v;
    }
    return best;
}

glm::mat4 placeOnSurface(const PickHit& hit, const glm::mat4& currentWorld, bool alignToNormal) {
    Transform t = Transform::fromMatrix(currentWorld);
    t.position = hit.point;
    if (alignToNormal) {
        const glm::vec3 up = t.rotation * glm::vec3(0.0f, 1.0f, 0.0f);
        t.rotation = glm::normalize(glm::rotation(up, glm::normalize(hit.normal)) * t.rotation);
    }
    return t.matrix();
}

// ---- bounds ------------------------------------------------------------------

Aabb selectionBounds(const Scene& scene, assets::AssetLibrary& library, const std::vector<NodeId>& ids) {
    Aabb b;
    for (NodeId id : scene.topLevelOnly(ids)) b.expand(scene.worldBounds(id, library));
    return b;
}

Transform translatedInWorld(const Scene& scene, NodeId id, const glm::vec3& worldDelta) {
    const Node* n = scene.find(id);
    if (!n) return {};
    Transform t = n->transform();
    t.position += glm::vec3(glm::inverse(scene.parentWorldMatrix(id)) * glm::vec4(worldDelta, 0.0f));
    return t;
}

// ---- arrays ------------------------------------------------------------------

std::unique_ptr<Command> linearArray(const Scene& scene, const std::vector<NodeId>& ids, int copies,
                                     const glm::vec3& offset) {
    return makeCopies(
        scene, ids, copies,
        [&](NodeId source, int k) { return translatedInWorld(scene, source, offset * static_cast<float>(k + 1)); },
        "Linear array");
}

std::unique_ptr<Command> gridArray(const Scene& scene, const std::vector<NodeId>& ids, const glm::ivec3& counts,
                                   const glm::vec3& spacing) {
    const glm::ivec3 c = glm::max(counts, glm::ivec3(1));
    std::vector<glm::vec3> offsets;
    for (int z = 0; z < c.z; ++z) {
        for (int y = 0; y < c.y; ++y) {
            for (int x = 0; x < c.x; ++x) {
                if (x == 0 && y == 0 && z == 0) continue;  // the original
                offsets.push_back(glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)) *
                                  spacing);
            }
        }
    }
    return makeCopies(
        scene, ids, static_cast<int>(offsets.size()),
        [&](NodeId source, int k) { return translatedInWorld(scene, source, offsets[static_cast<std::size_t>(k)]); },
        "Grid array");
}

std::unique_ptr<Command> circularArray(const Scene& scene, const std::vector<NodeId>& ids,
                                       const CircularArraySettings& s) {
    if (s.count < 2 || glm::length(s.axis) < 1e-6f) return nullptr;
    const bool fullCircle = std::abs(s.totalAngleDeg) >= 359.999f;
    const float step = fullCircle ? s.totalAngleDeg / static_cast<float>(s.count)
                                  : s.totalAngleDeg / static_cast<float>(s.count - 1);
    const glm::vec3 axis = glm::normalize(s.axis);
    return makeCopies(
        scene, ids, s.count - 1,
        [&](NodeId source, int k) {
            const glm::mat4 motion = glm::translate(glm::mat4(1.0f), s.centre) *
                                     glm::rotate(glm::mat4(1.0f), degToRad(step * static_cast<float>(k + 1)), axis) *
                                     glm::translate(glm::mat4(1.0f), -s.centre);
            if (s.rotateCopies) return movedBy(scene, source, motion);
            const glm::vec3 p = glm::vec3(scene.worldMatrix(source)[3]);
            return translatedInWorld(scene, source, glm::vec3(motion * glm::vec4(p, 1.0f)) - p);
        },
        "Circular array");
}

// ---- align / distribute / mirror ---------------------------------------------

std::unique_ptr<Command> alignNodes(const Scene& scene, assets::AssetLibrary& library, const std::vector<NodeId>& ids,
                                    Axis axis, AlignMode mode, std::optional<float> target) {
    const std::vector<NodeId> nodes = scene.topLevelOnly(ids);
    if (nodes.empty()) return nullptr;
    const int a = axisIndex(axis);
    const float goal = target ? *target : boundValue(selectionBounds(scene, library, nodes), a, mode);
    std::vector<std::pair<NodeId, Transform>> moves;
    for (NodeId id : nodes) {
        const Aabb b = scene.worldBounds(id, library);
        if (b.empty()) continue;
        moves.emplace_back(id, translatedInWorld(scene, id, unitAxis(a) * (goal - boundValue(b, a, mode))));
    }
    if (moves.empty()) return nullptr;
    return std::make_unique<SetTransformCommand>(std::move(moves), "Align");
}

std::unique_ptr<Command> distributeNodes(const Scene& scene, assets::AssetLibrary& library,
                                         const std::vector<NodeId>& ids, Axis axis, DistributeMode mode) {
    const int a = axisIndex(axis);
    struct Item {
        NodeId id;
        Aabb box;
    };
    std::vector<Item> items;
    for (NodeId id : scene.topLevelOnly(ids)) {
        const Aabb b = scene.worldBounds(id, library);
        if (!b.empty()) items.push_back({id, b});
    }
    if (items.size() < 3) return nullptr;
    std::stable_sort(items.begin(), items.end(),
                     [a](const Item& l, const Item& r) { return l.box.center()[a] < r.box.center()[a]; });
    const std::size_t n = items.size();
    std::vector<std::pair<NodeId, Transform>> moves;
    if (mode == DistributeMode::Centres) {
        const float first = items.front().box.center()[a];
        const float step = (items.back().box.center()[a] - first) / static_cast<float>(n - 1);
        for (std::size_t i = 1; i + 1 < n; ++i) {
            const float delta = first + step * static_cast<float>(i) - items[i].box.center()[a];
            moves.emplace_back(items[i].id, translatedInWorld(scene, items[i].id, unitAxis(a) * delta));
        }
    } else {
        float sizes = 0.0f;
        for (const Item& it : items) sizes += it.box.size()[a];
        const float span = items.back().box.max[a] - items.front().box.min[a];
        const float gap = (span - sizes) / static_cast<float>(n - 1);
        float cursor = items.front().box.max[a] + gap;
        for (std::size_t i = 1; i + 1 < n; ++i) {
            const float delta = cursor - items[i].box.min[a];
            moves.emplace_back(items[i].id, translatedInWorld(scene, items[i].id, unitAxis(a) * delta));
            cursor += items[i].box.size()[a] + gap;
        }
    }
    return std::make_unique<SetTransformCommand>(std::move(moves), "Distribute");
}

std::unique_ptr<Command> mirrorNodes(const Scene& scene, const std::vector<NodeId>& ids, Axis axis, float position,
                                     bool copy) {
    const int a = axisIndex(axis);
    const std::vector<NodeId> nodes = scene.sortedByTreeOrder(scene.topLevelOnly(ids));
    if (nodes.empty()) return nullptr;
    const glm::mat4 s = reflection(a);
    const glm::mat4 planeMirror =
        glm::translate(glm::mat4(1.0f), unitAxis(a) * position) * s * glm::translate(glm::mat4(1.0f), -unitAxis(a) * position);
    // A node's world W becomes planeMirror * W * S: the position is reflected
    // and the rotation conjugated, so the result is a proper (unmirrored) pose.
    auto mirroredRoot = [&](NodeId id) {
        const Node* n = scene.find(id);
        Transform t = scene.localFromWorld(n->parent(), planeMirror * scene.worldMatrix(id) * s);
        keepScale(t, n->transform().scale);
        return t;
    };
    std::function<void(NodeSnapshot&)> mirrorChildren = [&](NodeSnapshot& snap) {
        for (NodeSnapshot& c : snap.children) {
            c.transform = mirroredLocal(c.transform, a);
            mirrorChildren(c);
        }
    };

    if (copy) {
        std::vector<NodeInsertion> insertions;
        for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
            NodeInsertion ins;
            ins.snapshot = scene.snapshot(*it);
            ins.snapshot.clearIds();
            ins.snapshot.transform = mirroredRoot(*it);
            mirrorChildren(ins.snapshot);
            ins.parent = scene.find(*it)->parent();
            ins.index = scene.indexInParent(*it) + 1;
            insertions.push_back(std::move(ins));
        }
        return std::make_unique<InsertNodesCommand>("Mirror copy", std::move(insertions));
    }
    std::vector<std::pair<NodeId, Transform>> moves;
    for (NodeId id : nodes) {
        moves.emplace_back(id, mirroredRoot(id));
        for (NodeId d : scene.depthFirst(id)) moves.emplace_back(d, mirroredLocal(scene.find(d)->transform(), a));
    }
    return std::make_unique<SetTransformCommand>(std::move(moves), "Mirror");
}

std::unique_ptr<Command> dropToFloor(const Scene& scene, assets::AssetLibrary& library,
                                     const std::vector<NodeId>& ids, float floorY,
                                     const std::vector<MeshInstance>* obstacles) {
    Picker picker;
    std::vector<std::pair<NodeId, Transform>> moves;
    for (NodeId id : scene.topLevelOnly(ids)) {
        const Aabb b = scene.worldBounds(id, library);
        if (b.empty()) continue;
        float target = floorY;
        if (obstacles) {
            std::unordered_set<NodeId> own{id};
            for (NodeId d : scene.depthFirst(id)) own.insert(d);
            const PickFilter notOwn = [&own](const MeshInstance& i) { return own.count(i.pickId) == 0; };
            // Probe the centre and four inset corners of the bounds' footprint.
            const glm::vec3 c = b.center();
            const glm::vec3 h = b.size() * 0.4f;
            const glm::vec2 probes[] = {{0, 0}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
            for (const glm::vec2& p : probes) {
                Ray ray;
                ray.origin = {c.x + p.x * h.x, b.min.y + 1e-3f, c.z + p.y * h.z};
                ray.direction = {0.0f, -1.0f, 0.0f};
                if (auto hit = picker.pick(ray, *obstacles, library, notOwn)) target = std::max(target, hit->point.y);
            }
        }
        moves.emplace_back(id, translatedInWorld(scene, id, {0.0f, target - b.min.y, 0.0f}));
    }
    if (moves.empty()) return nullptr;
    return std::make_unique<SetTransformCommand>(std::move(moves), "Drop to floor");
}

// ---- hang on truss -------------------------------------------------------------

std::optional<HangResult> hangOnTruss(const Scene& scene, NodeId fixture, NodeId trussId, const glm::vec3& hitPoint,
                                      const HangOptions& options) {
    const Node* fx = scene.find(fixture);
    const Node* tn = scene.find(trussId);
    if (!fx || !tn || fixture == trussId) return std::nullopt;
    const TrussContent* truss = tn->as<TrussContent>();
    if (!truss) return std::nullopt;

    const glm::mat4 trussWorld = scene.worldMatrix(trussId);
    struct WorldChord {
        glm::vec3 a, b;
        bool horizontal;
    };
    std::vector<WorldChord> chords;
    float lowest = std::numeric_limits<float>::max();
    for (const truss::ChordLine& c : truss::trussChords(*truss)) {
        WorldChord w{glm::vec3(trussWorld * glm::vec4(c.a, 1.0f)), glm::vec3(trussWorld * glm::vec4(c.b, 1.0f)), false};
        const glm::vec3 d = w.b - w.a;
        const float len = glm::length(d);
        if (len < 1e-6f) continue;
        w.horizontal = std::abs(d.y) / len < 0.7f;  // less than ~45 degrees from level
        if (w.horizontal) lowest = std::min(lowest, (w.a.y + w.b.y) * 0.5f);
        chords.push_back(w);
    }
    if (chords.empty()) return std::nullopt;
    // Bottom chords: horizontal chords whose middle is at the lowest level (within one chord radius).
    const float r = truss::trussChordRadius(*truss);
    const bool anyHorizontal = lowest < std::numeric_limits<float>::max();
    const WorldChord* best = nullptr;
    glm::vec3 bestPoint(0.0f);
    float bestDist = std::numeric_limits<float>::max();
    for (const WorldChord& w : chords) {
        if (anyHorizontal && (!w.horizontal || (w.a.y + w.b.y) * 0.5f > lowest + r)) continue;
        const glm::vec3 d = w.b - w.a;
        const float t = std::clamp(glm::dot(hitPoint - w.a, d) / glm::dot(d, d), 0.0f, 1.0f);
        const glm::vec3 p = w.a + d * t;
        const float dist = glm::distance(p, hitPoint);
        if (dist < bestDist) {
            bestDist = dist;
            best = &w;
            bestPoint = p;
        }
    }
    if (!best) return std::nullopt;
    const glm::vec3 dir = glm::normalize(best->b - best->a);
    if (options.snapAlong > 0.0f) {
        const float length = glm::distance(best->a, best->b);
        const float along = std::clamp(snapValue(glm::dot(bestPoint - best->a, dir), options.snapAlong), 0.0f, length);
        bestPoint = best->a + dir * along;
    }

    HangResult result;
    result.chordPoint = bestPoint;
    result.chordDirection = dir;
    glm::vec3 origin;
    if (anyHorizontal) {
        // Hanging: the fixture's origin (top of its clamp/base) sits below the chord.
        origin = bestPoint - glm::vec3(0.0f, r + options.clampDrop, 0.0f);
    } else {
        // Vertical truss: clamp on the outside of the chord, away from the truss axis.
        const glm::vec3 axisPoint = glm::vec3(trussWorld[3]);
        glm::vec3 out = bestPoint - axisPoint;
        out.y = 0.0f;
        out = glm::length(out) > 1e-6f ? glm::normalize(out) : glm::vec3(0, 0, 1);
        origin = bestPoint + out * (r + options.clampDrop);
    }
    // Yaw only: local +Z toward the requested facing, projected on the floor plane.
    glm::vec3 facing = options.facing;
    facing.y = 0.0f;
    const float yaw = glm::length(facing) > 1e-6f ? std::atan2(facing.x, facing.z) : 0.0f;
    const glm::quat rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    result.world = glm::translate(glm::mat4(1.0f), origin) * glm::mat4_cast(rotation);

    result.parent = options.reparent ? trussId : fx->parent();
    if (result.parent != kInvalidNode && (result.parent == fixture || scene.isAncestor(fixture, result.parent)))
        return std::nullopt;  // the truss is inside the fixture's subtree
    result.local = scene.localFromWorld(result.parent, result.world);
    result.local.scale = fx->transform().scale;
    return result;
}

std::unique_ptr<Command> hangOnTrussCommand(const Scene& scene, NodeId fixture, NodeId truss,
                                            const glm::vec3& hitPoint, const HangOptions& options) {
    const std::optional<HangResult> hang = hangOnTruss(scene, fixture, truss, hitPoint, options);
    if (!hang) return nullptr;
    auto move = std::make_unique<SetTransformCommand>(fixture, hang->local, "Hang on truss");
    if (hang->parent == scene.find(fixture)->parent()) return move;
    std::vector<std::unique_ptr<Command>> steps;
    steps.push_back(std::make_unique<ReparentCommand>(std::vector<NodeId>{fixture}, hang->parent, -1, false));
    steps.push_back(std::move(move));
    return std::make_unique<CompoundCommand>("Hang on truss", std::move(steps));
}

}  // namespace dmxviz::stage::tools
