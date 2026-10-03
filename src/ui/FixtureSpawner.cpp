#include "ui/FixtureSpawner.h"

#include "core/Log.h"
#include "stage/Commands.h"
#include "stage/NodeFactory.h"
#include "stage/PlacementTools.h"
#include "ui/PatchModel.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace dmxviz::ui {

namespace {

constexpr float kSpotSpacing = 0.6f;       // metres between fixtures placed by "Add to scene"
constexpr float kSpotRadius = 0.4f;        // a spot is taken when a fixture is closer than this
constexpr float kStageCentreHeight = 3.0f;  // where fixtures appear when nothing is selected

int nextFixtureNumber(const stage::Scene& scene) {
    int highest = 0;
    for (const NodeId id : scene.nodesOfKind(stage::NodeKind::Fixture)) {
        const stage::Node* node = scene.find(id);
        const stage::FixtureContent* fixture = node != nullptr ? node->as<stage::FixtureContent>() : nullptr;
        if (fixture != nullptr) highest = std::max(highest, fixture->fixtureNumber);
    }
    return highest + 1;
}

// Another fixture hangs (almost) at this spot.
bool spotTaken(const stage::Scene& scene, const glm::vec3& spot) {
    for (const NodeId id : scene.nodesOfKind(stage::NodeKind::Fixture)) {
        const glm::vec3 p = glm::vec3(scene.worldMatrix(id)[3]);
        const float dx = p.x - spot.x;
        const float dz = p.z - spot.z;
        if (dx * dx + dz * dz < kSpotRadius * kSpotRadius && std::abs(p.y - spot.y) < 0.6f) return true;
    }
    return false;
}

// World pose of a fixture standing on a surface: its beam axis (local -Y) points away from
// the surface and its front (local +Z) faces the audience as far as the surface allows.
stage::Transform poseOnSurface(const glm::vec3& point, const glm::vec3& normal) {
    const glm::vec3 up = glm::normalize(normal);
    const glm::vec3 y = -up;
    glm::vec3 facing(0.0f, 0.0f, 1.0f);
    if (std::abs(glm::dot(facing, y)) > 0.95f) facing = glm::vec3(1.0f, 0.0f, 0.0f);  // wall faces the audience
    const glm::vec3 z = glm::normalize(facing - glm::dot(facing, y) * y);
    const glm::vec3 x = glm::cross(y, z);
    const glm::mat4 world(glm::vec4(x, 0.0f), glm::vec4(y, 0.0f), glm::vec4(z, 0.0f), glm::vec4(point, 1.0f));
    return stage::Transform::fromMatrix(world);
}

stage::Transform poseAt(const glm::vec3& point) {
    stage::Transform t;
    t.position = point;
    return t;
}

}  // namespace

NodeId FixtureSpawner::create(EditorContext& ctx, std::string_view typeId, std::string_view modeName,
                              const stage::Transform& placement, NodeId trussToHangOn, const glm::vec3& hangPoint) {
    const fixtures::FixtureType* type = ctx.fixtures.find(typeId);
    if (type == nullptr || type->modes.empty()) {
        log::warn("ui", "cannot add fixture: unknown fixture type '{}'", typeId);
        return kInvalidNode;
    }
    const fixtures::DmxMode* mode = modeName.empty() ? nullptr : type->findMode(modeName);
    if (mode == nullptr) mode = &type->modes.front();

    const PatchAllocator used(ctx.scene, ctx.fixtures);
    const stage::DmxPatch patch = used.findFree(mode->footprint);
    if (!patch.patched()) log::warn("ui", "no free DMX address for {}: fixture added unpatched", type->displayName());

    // Creating and hanging are separate commands, but the user should see one undo step.
    ctx.commands.beginBatch("Add fixture");
    auto add = std::make_unique<stage::AddNodeCommand>(
        stage::factory::fixture(std::string(typeId), mode->name, patch, nextFixtureNumber(ctx.scene)), placement);
    stage::AddNodeCommand* addRaw = add.get();
    const NodeId id = ctx.commands.execute(std::move(add)) != nullptr ? addRaw->createdId() : kInvalidNode;
    if (id != kInvalidNode && trussToHangOn != kInvalidNode) {
        if (auto hang = stage::tools::hangOnTrussCommand(ctx.scene, id, trussToHangOn, hangPoint))
            ctx.commands.execute(std::move(hang));
    }
    ctx.commands.endBatch();

    if (id != kInvalidNode) {
        ctx.selection.set(id);
        log::info("ui", "added {} at {}.{}", type->displayName(), patch.universe, patch.address);
    }
    return id;
}

NodeId FixtureSpawner::addAtHit(EditorContext& ctx, std::string_view typeId, std::string_view modeName,
                                const stage::PickHit& hit) {
    const stage::Node* target = ctx.scene.find(hit.node);
    if (target != nullptr && target->kind() == stage::NodeKind::Truss)
        return create(ctx, typeId, modeName, poseAt(hit.point), hit.node, hit.point);
    return create(ctx, typeId, modeName, poseOnSurface(hit.point, hit.normal), kInvalidNode, hit.point);
}

NodeId FixtureSpawner::addNearSelection(EditorContext& ctx, std::string_view typeId, std::string_view modeName) {
    // A selected truss gets the fixture, spread along its length.
    for (const NodeId id : ctx.selection.ids()) {
        const stage::Node* node = ctx.scene.find(id);
        const stage::TrussContent* truss = node != nullptr ? node->as<stage::TrussContent>() : nullptr;
        if (truss == nullptr) continue;

        const glm::mat4 world = ctx.scene.worldMatrix(id);
        const glm::vec3 centre = glm::vec3(world[3]);
        const glm::vec3 axis = glm::normalize(glm::vec3(world[0]));
        const float halfLength = truss->piece == stage::TrussPiece::Straight ? truss->straight.length * 0.5f : 0.0f;
        glm::vec3 spot = centre;
        for (float offset = 0.0f; offset <= halfLength; offset += kSpotSpacing) {
            const glm::vec3 right = centre + axis * offset;
            const glm::vec3 left = centre - axis * offset;
            if (!spotTaken(ctx.scene, right)) {
                spot = right;
                break;
            }
            if (offset > 0.0f && !spotTaken(ctx.scene, left)) {
                spot = left;
                break;
            }
        }
        return create(ctx, typeId, modeName, poseAt(spot), id, spot);
    }

    // Otherwise a free spot in rows around the stage centre.
    glm::vec3 spot(0.0f, kStageCentreHeight, 0.0f);
    for (int k = 0; k < 15 * 20; ++k) {
        const int ring = k % 15;
        const float x = static_cast<float>((ring + 1) / 2) * kSpotSpacing * (ring % 2 == 1 ? 1.0f : -1.0f);
        const glm::vec3 candidate(x, kStageCentreHeight, static_cast<float>(k / 15) * kSpotSpacing);
        if (!spotTaken(ctx.scene, candidate)) {
            spot = candidate;
            break;
        }
    }
    return create(ctx, typeId, modeName, poseAt(spot), kInvalidNode, spot);
}

}  // namespace dmxviz::ui
