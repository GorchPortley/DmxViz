#pragma once
// A node of the stage scene graph.

#include "core/Id.h"
#include "core/SceneTypes.h"
#include "stage/NodeContent.h"
#include "stage/Transform.h"

#include <optional>
#include <string>
#include <vector>

namespace dmxviz::stage {

// Everything about a node the user edits in the inspector except its
// transform and its place in the hierarchy. Property-edit commands snapshot
// this struct for undo, so keep it a plain value type.
struct NodeData {
    std::string name;
    bool visible = true;
    bool locked = false;
    int layer = 0;                              // index into Scene::layers()
    glm::vec3 color{0.8f};                      // UI tag colour (outliner, wireframe), linear RGB
    std::optional<Material> materialOverride;   // replaces the material of every emitted part when set
    NodeContent content;

    NodeKind kind() const { return kindOf(content); }
    template <typename T>
    T* as() {
        return std::get_if<T>(&content);
    }
    template <typename T>
    const T* as() const {
        return std::get_if<T>(&content);
    }
};

// Convenience constructor: data for a new node of `kind` with default content
// and the kind's label as name.
NodeData makeNodeData(NodeKind kind, std::string name = {});
NodeData makeNodeData(NodeContent content, std::string name = {});

// One mesh of a node's geometry, relative to the node (see NodeGeometry.h).
struct RenderPart {
    MeshId mesh = kInvalidMesh;
    glm::mat4 local{1.0f};
    Material material;
};

// A scene graph node: identity, hierarchy links, local transform and data.
// Nodes are owned by the Scene and changed only through it (so caches and
// revision counters stay correct); everything here is read-only.
class Node {
public:
    NodeId id() const { return id_; }
    NodeId parent() const { return parent_; }  // kInvalidNode for top-level nodes
    const std::vector<NodeId>& children() const { return children_; }
    const Transform& transform() const { return transform_; }
    const NodeData& data() const { return data_; }

    NodeKind kind() const { return data_.kind(); }
    const std::string& name() const { return data_.name; }
    template <typename T>
    const T* as() const {
        return data_.as<T>();
    }

private:
    friend class Scene;

    NodeId id_ = kInvalidNode;
    NodeId parent_ = kInvalidNode;
    std::vector<NodeId> children_;
    Transform transform_;
    NodeData data_;

    // World matrix cache. Invariant: a dirty node has only dirty descendants,
    // so marking a subtree dirty can stop at the first already-dirty node.
    mutable glm::mat4 world_{1.0f};
    mutable bool worldDirty_ = true;
};

// A detached copy of a node and its subtree (undo of delete, duplicate,
// clipboard). id == kInvalidNode means "assign a fresh id on insertion".
struct NodeSnapshot {
    NodeId id = kInvalidNode;
    Transform transform;
    NodeData data;
    std::vector<NodeSnapshot> children;

    // Sets every id in the subtree to kInvalidNode (for copies).
    void clearIds();
    std::size_t nodeCount() const;
};

}  // namespace dmxviz::stage
