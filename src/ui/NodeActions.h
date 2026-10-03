#pragma once
// NodeActions: the user-level scene edits that several panels share (delete,
// duplicate, group, reparent, add a node...). Each one builds a stage command,
// runs it through ctx.commands (so it is undoable) and then fixes up the
// selection. Panels call these instead of repeating that boilerplate.

#include "stage/Node.h"
#include "ui/EditorContext.h"

#include <string>
#include <vector>

namespace dmxviz::ui::actions {

// The nodes of `ids` that a tool may change: existing, top-most of their
// subtree (children follow their parent) and not locked.
std::vector<NodeId> editableTopLevel(const EditorContext& ctx, const std::vector<NodeId>& ids);

// Selects the nodes the command created (if any) and drops stale selection entries.
void selectResultOf(EditorContext& ctx, stage::Command* executed);

void deleteNodes(EditorContext& ctx, const std::vector<NodeId>& ids);
void duplicateNodes(EditorContext& ctx, const std::vector<NodeId>& ids);
void groupNodes(EditorContext& ctx, const std::vector<NodeId>& ids);
void ungroupNodes(EditorContext& ctx, const std::vector<NodeId>& ids);
void renameNode(EditorContext& ctx, NodeId id, const std::string& name);
void setVisible(EditorContext& ctx, const std::vector<NodeId>& ids, bool visible);
void setLocked(EditorContext& ctx, const std::vector<NodeId>& ids, bool locked);
// Moves nodes under `parent` (kInvalidNode = top level) at sibling `index` (< 0 appends), keeping their world pose.
void reparentNodes(EditorContext& ctx, const std::vector<NodeId>& ids, NodeId parent, int index);

// Points the viewport camera at the nodes (or the whole stage when `ids` is empty).
void frameNodes(EditorContext& ctx, const std::vector<NodeId>& ids);

// Where a new node should appear: the viewport camera's target dropped onto the
// floor and snapped to a 0.25 m grid, or the scene origin without a camera.
glm::vec3 placementPoint(const EditorContext& ctx);

// Adds one node at `transform`, selects it and returns its id.
NodeId addNode(EditorContext& ctx, stage::NodeData data, const stage::Transform& transform);
// Same, placed at placementPoint() raised by `height` metres.
NodeId addNodeAtPlacement(EditorContext& ctx, stage::NodeData data, float height = 0.0f);

}  // namespace dmxviz::ui::actions
