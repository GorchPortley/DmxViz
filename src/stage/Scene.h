#pragma once
// The stage scene graph.

#include "assets/AssetLibrary.h"
#include "core/SceneTypes.h"
#include "stage/ModelCache.h"
#include "stage/Node.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace dmxviz::stage {

class Selection;

// A named set of nodes that can be hidden or locked together (like CAD layers
// or Vectorworks classes). Every node is on exactly one layer.
struct Layer {
    std::string name = "Default";
    bool visible = true;
    bool locked = false;
    glm::vec3 color{0.8f};
};

// Owns all nodes of a stage: their hierarchy, transforms and data, plus caches
// derived from them (world matrices, generated meshes, loaded models).
//
// * Node ids are assigned by the scene, never reused (also not after deleting
//   and saving: the next id is persisted) and stable across undo/redo.
// * Top-level nodes have parent kInvalidNode; their order is roots().
// * The mutators below are the low-level API used by commands and the project
//   loader. User edits should go through a CommandStack so they can be undone.
// * Main thread only.
class Scene {
public:
    Scene();
    ~Scene();
    Scene(Scene&&) noexcept;
    Scene& operator=(Scene&&) noexcept;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    // ---- lookup ---------------------------------------------------------
    const Node* find(NodeId id) const;
    bool contains(NodeId id) const { return find(id) != nullptr; }
    std::size_t nodeCount() const { return nodes_.size(); }
    bool empty() const { return nodes_.empty(); }

    const std::vector<NodeId>& roots() const { return roots_; }
    // Children of `parent`; kInvalidNode returns the top-level nodes.
    const std::vector<NodeId>& childrenOf(NodeId parent) const;
    // Position of `id` among its siblings, -1 if unknown.
    int indexInParent(NodeId id) const;
    // Pre-order traversal of the subtree under `root` (or the whole scene),
    // excluding `root` itself when it is a node id.
    std::vector<NodeId> depthFirst(NodeId root = kInvalidNode) const;
    // True when `ancestor` is a strict ancestor of `node`.
    bool isAncestor(NodeId ancestor, NodeId node) const;
    // Removes ids whose ancestor is also in the list, and unknown ids. Keeps order.
    std::vector<NodeId> topLevelOnly(const std::vector<NodeId>& ids) const;
    // Sorts ids into scene tree (pre-order) order.
    std::vector<NodeId> sortedByTreeOrder(const std::vector<NodeId>& ids) const;
    // First node with this name (depth-first), kInvalidNode if none.
    NodeId findByName(std::string_view name) const;
    // All nodes of one kind in tree order (e.g. every fixture for the simulation).
    std::vector<NodeId> nodesOfKind(NodeKind kind) const;

    // ---- transforms -----------------------------------------------------
    // Cached; recomputed lazily after a transform or hierarchy change.
    glm::mat4 worldMatrix(NodeId id) const;
    glm::mat4 parentWorldMatrix(NodeId id) const;
    Transform worldTransform(NodeId id) const { return Transform::fromMatrix(worldMatrix(id)); }
    // The local transform that puts a child of `parent` at `world`.
    Transform localFromWorld(NodeId parent, const glm::mat4& world) const;

    // Visible/locked taking ancestors and the node's layer into account.
    bool effectiveVisible(NodeId id) const;
    bool effectiveLocked(NodeId id) const;

    // ---- layers ---------------------------------------------------------
    const std::vector<Layer>& layers() const { return layers_; }
    // Replaces the layer table (at least one layer is always kept).
    void setLayers(std::vector<Layer> layers);
    const Layer& layerOf(NodeId id) const;

    // ---- low-level mutation (prefer commands for user edits) -------------
    // Adds a node and returns its new id. index < 0 appends.
    NodeId addNode(NodeData data, const Transform& transform = {}, NodeId parent = kInvalidNode, int index = -1);
    // Inserts a subtree. Snapshot ids are kept when free, otherwise (or when
    // kInvalidNode) a fresh id is assigned. Returns the root id, or
    // kInvalidNode if `parent` does not exist.
    NodeId insertSnapshot(const NodeSnapshot& snapshot, NodeId parent = kInvalidNode, int index = -1);
    NodeSnapshot snapshot(NodeId id) const;
    // Removes the node and its whole subtree.
    bool removeNode(NodeId id);
    // Moves a node (keeping its local transform). Fails if newParent is the
    // node itself or inside its subtree. index < 0 appends.
    bool setParent(NodeId id, NodeId newParent, int index = -1);
    bool setTransform(NodeId id, const Transform& transform);
    bool setData(NodeId id, NodeData data);
    void clear();

    // ---- ids ------------------------------------------------------------
    NodeId nextId() const { return nextId_; }
    // Used by the project loader; never moves the counter backwards.
    void setNextId(NodeId next);

    // ---- change tracking ------------------------------------------------
    // Incremented on every change; cheap way for views to know when to rebuild.
    std::uint64_t revision() const { return revision_; }
    // Incremented when nodes are added, removed, renamed or moved in the hierarchy.
    std::uint64_t structureRevision() const { return structureRevision_; }

    // ---- geometry -------------------------------------------------------
    // Appends world-space MeshInstances for every visible node with geometry
    // (everything except fixtures, cameras and groups). pickId is the node id;
    // highlight comes from `selection` (a selected or hovered group highlights
    // its whole subtree). Generated meshes are built and cached on demand.
    void collectRenderables(assets::AssetLibrary& library, std::vector<MeshInstance>& out,
                            const Selection* selection = nullptr) const;

    // The meshes of one node in node space (cached until the node changes).
    const std::vector<RenderPart>& renderParts(NodeId id, assets::AssetLibrary& library) const;
    // Bounds of the node's own geometry in node space; empty if it has none.
    Aabb localBounds(NodeId id, assets::AssetLibrary& library) const;
    // World-space bounds of a node (and its subtree). Nodes without geometry
    // (fixtures, groups, cameras) contribute their world position.
    Aabb worldBounds(NodeId id, assets::AssetLibrary& library, bool includeChildren = true) const;

    ModelCache& models() const { return *models_; }
    // Drops cached geometry so models are reloaded from disk and generated
    // meshes rebuilt (e.g. after switching AssetLibrary).
    void invalidateGeometry() const;

private:
    Node* findMutable(NodeId id);
    std::vector<NodeId>& childListOf(NodeId parent);
    void markWorldDirty(const Node& node) const;
    void touch(bool structural);
    void insertRecursive(const NodeSnapshot& s, NodeId parent, int index, NodeId& rootOut);
    void collectNode(const Node& n, assets::AssetLibrary& library, std::vector<MeshInstance>& out,
                     const Selection* selection, Highlight inherited) const;

    std::unordered_map<NodeId, std::unique_ptr<Node>> nodes_;
    std::vector<NodeId> roots_;
    std::vector<Layer> layers_;
    NodeId nextId_ = 1;
    std::uint64_t revision_ = 1;
    std::uint64_t structureRevision_ = 1;

    // Derived data.
    mutable std::unordered_map<NodeId, std::vector<RenderPart>> partsCache_;
    mutable const assets::AssetLibrary* partsLibrary_ = nullptr;
    std::unique_ptr<ModelCache> models_;
};

}  // namespace dmxviz::stage
