#include "ui/NodeActions.h"

#include "core/Log.h"
#include "stage/Commands.h"
#include "stage/PlacementTools.h"
#include "ui/ViewportCamera.h"

#include <algorithm>
#include <memory>

namespace dmxviz::ui::actions {

namespace {

constexpr float kPlacementGrid = 0.25f;              // metres
const glm::vec3 kDuplicateOffset{0.5f, 0.0f, 0.0f};  // so a copy is visible next to its original

}  // namespace

std::vector<NodeId> editableTopLevel(const EditorContext& ctx, const std::vector<NodeId>& ids) {
    std::vector<NodeId> result;
    for (NodeId id : ctx.scene.topLevelOnly(ids))
        if (!ctx.scene.effectiveLocked(id)) result.push_back(id);
    return result;
}

void selectResultOf(EditorContext& ctx, stage::Command* executed) {
    if (executed != nullptr) {
        const std::vector<NodeId> created = executed->resultNodes();
        if (!created.empty()) ctx.selection.setMany(created);
    }
    ctx.selection.setHover(kInvalidNode);
    ctx.selection.prune(ctx.scene);
}

void deleteNodes(EditorContext& ctx, const std::vector<NodeId>& ids) {
    const std::vector<NodeId> targets = editableTopLevel(ctx, ids);
    if (targets.empty()) return;
    ctx.commands.execute(std::make_unique<stage::DeleteNodesCommand>(targets));
    selectResultOf(ctx, nullptr);
    log::debug("ui", "deleted {} node(s)", targets.size());
}

void duplicateNodes(EditorContext& ctx, const std::vector<NodeId>& ids) {
    const std::vector<NodeId> targets = editableTopLevel(ctx, ids);
    if (targets.empty()) return;
    selectResultOf(ctx, ctx.commands.execute(stage::makeDuplicateCommand(ctx.scene, targets, kDuplicateOffset)));
}

void groupNodes(EditorContext& ctx, const std::vector<NodeId>& ids) {
    const std::vector<NodeId> targets = editableTopLevel(ctx, ids);
    if (targets.empty()) return;
    selectResultOf(ctx, ctx.commands.execute(std::make_unique<stage::GroupCommand>(targets)));
}

void ungroupNodes(EditorContext& ctx, const std::vector<NodeId>& ids) {
    std::vector<NodeId> groups;
    for (NodeId id : editableTopLevel(ctx, ids)) {
        const stage::Node* node = ctx.scene.find(id);
        if (node != nullptr && node->kind() == stage::NodeKind::Group) groups.push_back(id);
    }
    if (groups.empty()) return;
    selectResultOf(ctx, ctx.commands.execute(std::make_unique<stage::UngroupCommand>(groups)));
}

void renameNode(EditorContext& ctx, NodeId id, const std::string& name) {
    const stage::Node* node = ctx.scene.find(id);
    if (node == nullptr || name.empty() || name == node->name()) return;
    ctx.commands.execute(stage::makeEditCommand(ctx.scene, {id}, "Rename", [&](stage::NodeData& d) { d.name = name; }));
}

void setVisible(EditorContext& ctx, const std::vector<NodeId>& ids, bool visible) {
    ctx.commands.execute(stage::makeEditCommand(ctx.scene, ids, visible ? "Show" : "Hide",
                                                [visible](stage::NodeData& d) { d.visible = visible; }));
}

void setLocked(EditorContext& ctx, const std::vector<NodeId>& ids, bool locked) {
    ctx.commands.execute(stage::makeEditCommand(ctx.scene, ids, locked ? "Lock" : "Unlock",
                                                [locked](stage::NodeData& d) { d.locked = locked; }));
}

void reparentNodes(EditorContext& ctx, const std::vector<NodeId>& ids, NodeId parent, int index) {
    if (ids.empty()) return;
    ctx.commands.execute(std::make_unique<stage::ReparentCommand>(ids, parent, index, true));
}

void frameNodes(EditorContext& ctx, const std::vector<NodeId>& ids) {
    if (ctx.viewportCamera == nullptr) return;
    Aabb bounds;
    if (!ids.empty()) {
        bounds = stage::tools::selectionBounds(ctx.scene, ctx.assets, ids);
    } else {
        for (NodeId root : ctx.scene.roots()) bounds.expand(ctx.scene.worldBounds(root, ctx.assets));
    }
    if (!bounds.empty()) ctx.viewportCamera->frame(bounds);
}

glm::vec3 placementPoint(const EditorContext& ctx) {
    if (ctx.viewportCamera == nullptr) return glm::vec3(0.0f);
    const glm::vec3 target = ctx.viewportCamera->target();
    return glm::vec3(stage::tools::snapValue(target.x, kPlacementGrid), 0.0f,
                     stage::tools::snapValue(target.z, kPlacementGrid));
}

NodeId addNode(EditorContext& ctx, stage::NodeData data, const stage::Transform& transform) {
    stage::Command* done = ctx.commands.execute(std::make_unique<stage::AddNodeCommand>(std::move(data), transform));
    selectResultOf(ctx, done);
    return ctx.selection.primary();
}

NodeId addNodeAtPlacement(EditorContext& ctx, stage::NodeData data, float height) {
    stage::Transform t;
    t.position = placementPoint(ctx) + glm::vec3(0.0f, height, 0.0f);
    return addNode(ctx, std::move(data), t);
}

}  // namespace dmxviz::ui::actions
