#include "stage/Scene.h"

#include "stage/NodeGeometry.h"
#include "stage/Selection.h"

#include <algorithm>
#include <unordered_set>

namespace dmxviz::stage {
namespace {

const std::vector<NodeId> kNoChildren;
const std::vector<RenderPart> kNoParts;

void insertAt(std::vector<NodeId>& list, NodeId id, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= list.size())
        list.push_back(id);
    else
        list.insert(list.begin() + index, id);
}

}  // namespace

Scene::Scene() : layers_{Layer{}}, models_(std::make_unique<ModelCache>()) {}
Scene::~Scene() = default;
Scene::Scene(Scene&&) noexcept = default;
Scene& Scene::operator=(Scene&&) noexcept = default;

// ---- lookup ----------------------------------------------------------------

const Node* Scene::find(NodeId id) const {
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : it->second.get();
}

Node* Scene::findMutable(NodeId id) {
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : it->second.get();
}

const std::vector<NodeId>& Scene::childrenOf(NodeId parent) const {
    if (parent == kInvalidNode) return roots_;
    const Node* n = find(parent);
    return n ? n->children_ : kNoChildren;
}

std::vector<NodeId>& Scene::childListOf(NodeId parent) {
    if (parent == kInvalidNode) return roots_;
    return findMutable(parent)->children_;
}

int Scene::indexInParent(NodeId id) const {
    const Node* n = find(id);
    if (!n) return -1;
    const std::vector<NodeId>& siblings = childrenOf(n->parent_);
    auto it = std::find(siblings.begin(), siblings.end(), id);
    return it == siblings.end() ? -1 : static_cast<int>(it - siblings.begin());
}

std::vector<NodeId> Scene::depthFirst(NodeId root) const {
    std::vector<NodeId> out;
    std::vector<NodeId> stack;
    const std::vector<NodeId>& top = childrenOf(root);
    for (auto it = top.rbegin(); it != top.rend(); ++it) stack.push_back(*it);
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        out.push_back(id);
        const std::vector<NodeId>& kids = childrenOf(id);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) stack.push_back(*it);
    }
    return out;
}

bool Scene::isAncestor(NodeId ancestor, NodeId node) const {
    if (ancestor == kInvalidNode) return false;
    const Node* n = find(node);
    while (n && n->parent_ != kInvalidNode) {
        if (n->parent_ == ancestor) return true;
        n = find(n->parent_);
    }
    return false;
}

std::vector<NodeId> Scene::topLevelOnly(const std::vector<NodeId>& ids) const {
    const std::unordered_set<NodeId> set(ids.begin(), ids.end());
    std::unordered_set<NodeId> seen;
    std::vector<NodeId> out;
    for (NodeId id : ids) {
        if (!contains(id) || !seen.insert(id).second) continue;
        bool covered = false;
        for (const Node* n = find(id); n && n->parent_ != kInvalidNode; n = find(n->parent_)) {
            if (set.count(n->parent_)) {
                covered = true;
                break;
            }
        }
        if (!covered) out.push_back(id);
    }
    return out;
}

std::vector<NodeId> Scene::sortedByTreeOrder(const std::vector<NodeId>& ids) const {
    const std::unordered_set<NodeId> set(ids.begin(), ids.end());
    std::vector<NodeId> out;
    for (NodeId id : depthFirst()) {
        if (set.count(id)) out.push_back(id);
    }
    return out;
}

NodeId Scene::findByName(std::string_view name) const {
    for (NodeId id : depthFirst()) {
        if (find(id)->data_.name == name) return id;
    }
    return kInvalidNode;
}

std::vector<NodeId> Scene::nodesOfKind(NodeKind kind) const {
    std::vector<NodeId> out;
    for (NodeId id : depthFirst()) {
        if (find(id)->kind() == kind) out.push_back(id);
    }
    return out;
}

// ---- transforms ------------------------------------------------------------

glm::mat4 Scene::worldMatrix(NodeId id) const {
    const Node* n = find(id);
    if (!n) return glm::mat4(1.0f);
    if (n->worldDirty_) {
        n->world_ = parentWorldMatrix(id) * n->transform_.matrix();
        n->worldDirty_ = false;
    }
    return n->world_;
}

glm::mat4 Scene::parentWorldMatrix(NodeId id) const {
    const Node* n = find(id);
    if (!n || n->parent_ == kInvalidNode) return glm::mat4(1.0f);
    return worldMatrix(n->parent_);
}

Transform Scene::localFromWorld(NodeId parent, const glm::mat4& world) const {
    const glm::mat4 parentWorld = parent == kInvalidNode ? glm::mat4(1.0f) : worldMatrix(parent);
    return Transform::fromMatrix(glm::inverse(parentWorld) * world);
}

void Scene::markWorldDirty(const Node& node) const {
    if (node.worldDirty_) return;  // its subtree is already dirty (see Node)
    node.worldDirty_ = true;
    for (NodeId child : node.children_) {
        if (const Node* c = find(child)) markWorldDirty(*c);
    }
}

bool Scene::effectiveVisible(NodeId id) const {
    const Node* n = find(id);
    if (!n) return false;
    for (; n; n = find(n->parent_)) {
        if (!n->data_.visible || !layerOf(n->id_).visible) return false;
    }
    return true;
}

bool Scene::effectiveLocked(NodeId id) const {
    for (const Node* n = find(id); n; n = find(n->parent_)) {
        if (n->data_.locked || layerOf(n->id_).locked) return true;
    }
    return false;
}

// ---- layers ----------------------------------------------------------------

void Scene::setLayers(std::vector<Layer> layers) {
    if (layers.empty()) layers.push_back(Layer{});
    layers_ = std::move(layers);
    touch(true);
}

const Layer& Scene::layerOf(NodeId id) const {
    const Node* n = find(id);
    if (!n || n->data_.layer < 0 || static_cast<std::size_t>(n->data_.layer) >= layers_.size()) return layers_.front();
    return layers_[static_cast<std::size_t>(n->data_.layer)];
}

// ---- mutation --------------------------------------------------------------

void Scene::touch(bool structural) {
    ++revision_;
    if (structural) ++structureRevision_;
}

NodeId Scene::addNode(NodeData data, const Transform& transform, NodeId parent, int index) {
    if (parent != kInvalidNode && !contains(parent)) return kInvalidNode;
    auto node = std::make_unique<Node>();
    node->id_ = nextId_++;
    node->parent_ = parent;
    node->transform_ = transform;
    node->data_ = std::move(data);
    const NodeId id = node->id_;
    nodes_.emplace(id, std::move(node));
    insertAt(childListOf(parent), id, index);
    touch(true);
    return id;
}

void Scene::insertRecursive(const NodeSnapshot& s, NodeId parent, int index, NodeId& rootOut) {
    auto node = std::make_unique<Node>();
    const bool keepId = s.id != kInvalidNode && !contains(s.id);
    node->id_ = keepId ? s.id : nextId_;
    nextId_ = std::max(nextId_, node->id_ + 1);
    node->parent_ = parent;
    node->transform_ = s.transform;
    node->data_ = s.data;
    const NodeId id = node->id_;
    nodes_.emplace(id, std::move(node));
    insertAt(childListOf(parent), id, index);
    rootOut = id;
    for (const NodeSnapshot& child : s.children) {
        NodeId ignored = kInvalidNode;
        insertRecursive(child, id, -1, ignored);
    }
}

NodeId Scene::insertSnapshot(const NodeSnapshot& snapshot, NodeId parent, int index) {
    if (parent != kInvalidNode && !contains(parent)) return kInvalidNode;
    NodeId root = kInvalidNode;
    insertRecursive(snapshot, parent, index, root);
    touch(true);
    return root;
}

NodeSnapshot Scene::snapshot(NodeId id) const {
    NodeSnapshot s;
    const Node* n = find(id);
    if (!n) return s;
    s.id = n->id_;
    s.transform = n->transform_;
    s.data = n->data_;
    s.children.reserve(n->children_.size());
    for (NodeId c : n->children_) s.children.push_back(snapshot(c));
    return s;
}

bool Scene::removeNode(NodeId id) {
    Node* n = findMutable(id);
    if (!n) return false;
    std::vector<NodeId>& siblings = childListOf(n->parent_);
    siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());
    std::vector<NodeId> doomed = depthFirst(id);
    doomed.push_back(id);
    for (NodeId d : doomed) {
        nodes_.erase(d);
        partsCache_.erase(d);
    }
    touch(true);
    return true;
}

bool Scene::setParent(NodeId id, NodeId newParent, int index) {
    Node* n = findMutable(id);
    if (!n) return false;
    if (newParent != kInvalidNode && (!contains(newParent) || newParent == id || isAncestor(id, newParent)))
        return false;
    std::vector<NodeId>& oldList = childListOf(n->parent_);
    oldList.erase(std::remove(oldList.begin(), oldList.end(), id), oldList.end());
    n->parent_ = newParent;
    insertAt(childListOf(newParent), id, index);
    markWorldDirty(*n);
    touch(true);
    return true;
}

bool Scene::setTransform(NodeId id, const Transform& transform) {
    Node* n = findMutable(id);
    if (!n) return false;
    n->transform_ = transform;
    markWorldDirty(*n);
    touch(false);
    return true;
}

bool Scene::setData(NodeId id, NodeData data) {
    Node* n = findMutable(id);
    if (!n) return false;
    n->data_ = std::move(data);
    partsCache_.erase(id);
    touch(true);
    return true;
}

void Scene::clear() {
    nodes_.clear();
    roots_.clear();
    partsCache_.clear();
    layers_ = {Layer{}};
    touch(true);
}

void Scene::setNextId(NodeId next) { nextId_ = std::max(nextId_, next); }

// ---- geometry --------------------------------------------------------------

const std::vector<RenderPart>& Scene::renderParts(NodeId id, assets::AssetLibrary& library) const {
    const Node* n = find(id);
    if (!n) return kNoParts;
    if (partsLibrary_ != &library) {
        // Mesh ids belong to one library; start over with a different one.
        partsCache_.clear();
        partsLibrary_ = &library;
    }
    auto it = partsCache_.find(id);
    if (it == partsCache_.end()) it = partsCache_.emplace(id, buildRenderParts(n->data_, library, *models_)).first;
    return it->second;
}

Aabb Scene::localBounds(NodeId id, assets::AssetLibrary& library) const {
    Aabb box;
    for (const RenderPart& p : renderParts(id, library)) {
        if (const assets::MeshData* mesh = library.mesh(p.mesh)) box.expand(mesh->bounds.transformed(p.local));
    }
    return box;
}

Aabb Scene::worldBounds(NodeId id, assets::AssetLibrary& library, bool includeChildren) const {
    Aabb box;
    const Node* n = find(id);
    if (!n) return box;
    const Aabb local = localBounds(id, library);
    if (!local.empty())
        box.expand(local.transformed(worldMatrix(id)));
    else if (n->children_.empty() || !includeChildren)
        box.expand(glm::vec3(worldMatrix(id)[3]));  // fixtures, cameras: their origin
    if (includeChildren) {
        for (NodeId c : n->children_) box.expand(worldBounds(c, library, true));
    }
    return box;
}

void Scene::invalidateGeometry() const {
    partsCache_.clear();
    models_->clear();
}

void Scene::collectRenderables(assets::AssetLibrary& library, std::vector<MeshInstance>& out,
                               const Selection* selection) const {
    for (NodeId id : roots_) {
        if (const Node* n = find(id)) collectNode(*n, library, out, selection, Highlight::None);
    }
}

void Scene::collectNode(const Node& n, assets::AssetLibrary& library, std::vector<MeshInstance>& out,
                        const Selection* selection, Highlight inherited) const {
    if (!n.data_.visible || !layerOf(n.id_).visible) return;
    Highlight highlight = inherited;
    if (selection) {
        const Highlight own = selection->highlightFor(n.id_);
        if (own == Highlight::Selected || (own == Highlight::Hover && highlight == Highlight::None)) highlight = own;
    }
    const std::vector<RenderPart>& parts = renderParts(n.id_, library);
    if (!parts.empty()) {
        const glm::mat4 world = worldMatrix(n.id_);
        for (const RenderPart& p : parts) {
            MeshInstance inst;
            inst.mesh = p.mesh;
            inst.world = world * p.local;
            inst.material = p.material;
            inst.pickId = n.id_;
            inst.highlight = highlight;
            out.push_back(inst);
        }
    }
    for (NodeId c : n.children_) {
        if (const Node* child = find(c)) collectNode(*child, library, out, selection, highlight);
    }
}

}  // namespace dmxviz::stage
