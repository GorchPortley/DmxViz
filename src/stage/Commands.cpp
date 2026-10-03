#include "stage/Commands.h"

#include <algorithm>
#include <unordered_set>

namespace dmxviz::stage {

// ---- CompoundCommand ---------------------------------------------------------

CompoundCommand::CompoundCommand(std::string name, std::vector<std::unique_ptr<Command>> commands)
    : name_(std::move(name)), commands_(std::move(commands)) {}

bool CompoundCommand::apply(Scene& scene) {
    for (std::size_t i = 0; i < commands_.size(); ++i) {
        if (!commands_[i]->apply(scene)) {
            for (std::size_t j = i; j-- > 0;) commands_[j]->revert(scene);
            return false;
        }
    }
    return !commands_.empty();
}

void CompoundCommand::revert(Scene& scene) {
    for (std::size_t j = commands_.size(); j-- > 0;) commands_[j]->revert(scene);
}

std::vector<NodeId> CompoundCommand::resultNodes() const {
    std::vector<NodeId> out;
    for (const auto& c : commands_) {
        const std::vector<NodeId> r = c->resultNodes();
        out.insert(out.end(), r.begin(), r.end());
    }
    return out;
}

// ---- InsertNodesCommand ------------------------------------------------------

InsertNodesCommand::InsertNodesCommand(std::string name, std::vector<NodeInsertion> insertions)
    : name_(std::move(name)), insertions_(std::move(insertions)) {}

bool InsertNodesCommand::apply(Scene& scene) {
    created_.clear();
    for (NodeInsertion& ins : insertions_) {
        const NodeId id = scene.insertSnapshot(ins.snapshot, ins.parent, ins.index);
        if (id == kInvalidNode) {
            revert(scene);
            created_.clear();
            return false;
        }
        // Remember the ids that were assigned so that redo re-creates the same nodes.
        ins.snapshot = scene.snapshot(id);
        created_.push_back(id);
    }
    return !created_.empty();
}

void InsertNodesCommand::revert(Scene& scene) {
    for (std::size_t i = created_.size(); i-- > 0;) scene.removeNode(created_[i]);
}

AddNodeCommand::AddNodeCommand(NodeData data, const Transform& transform, NodeId parent, int index)
    : InsertNodesCommand("Add " + data.name, {}) {
    addInsertion({NodeSnapshot{kInvalidNode, transform, std::move(data), {}}, parent, index});
}

NodeId AddNodeCommand::createdId() const {
    const std::vector<NodeId> r = resultNodes();
    return r.empty() ? kInvalidNode : r.front();
}

std::unique_ptr<InsertNodesCommand> makeDuplicateCommand(const Scene& scene, const std::vector<NodeId>& ids,
                                                         const glm::vec3& worldOffset) {
    const std::vector<NodeId> sorted = scene.sortedByTreeOrder(scene.topLevelOnly(ids));
    std::vector<NodeInsertion> insertions;
    // Last sibling first, so inserting a copy never shifts the index of an
    // original that is processed later.
    for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
        const Node* n = scene.find(*it);
        NodeInsertion ins;
        ins.snapshot = scene.snapshot(*it);
        ins.snapshot.clearIds();
        ins.parent = n->parent();
        ins.index = scene.indexInParent(*it) + 1;
        if (worldOffset != glm::vec3(0.0f)) {
            const glm::mat4 world = glm::translate(glm::mat4(1.0f), worldOffset) * scene.worldMatrix(*it);
            Transform t = scene.localFromWorld(ins.parent, world);
            // Only the position should change; keep rotation and scale bit-exact.
            ins.snapshot.transform.position = t.position;
        }
        insertions.push_back(std::move(ins));
    }
    return std::make_unique<InsertNodesCommand>("Duplicate", std::move(insertions));
}

std::unique_ptr<InsertNodesCommand> makePasteCommand(std::vector<NodeSnapshot> snapshots, NodeId parent) {
    std::vector<NodeInsertion> insertions;
    for (NodeSnapshot& s : snapshots) {
        s.clearIds();
        insertions.push_back({std::move(s), parent, -1});
    }
    return std::make_unique<InsertNodesCommand>("Paste", std::move(insertions));
}

// ---- DeleteNodesCommand ------------------------------------------------------

DeleteNodesCommand::DeleteNodesCommand(std::vector<NodeId> ids) : ids_(std::move(ids)) {}

bool DeleteNodesCommand::apply(Scene& scene) {
    removed_.clear();
    const std::vector<NodeId> ids = scene.topLevelOnly(ids_);
    if (ids.empty()) return false;
    for (NodeId id : ids) {
        Removed r;
        r.snapshot = scene.snapshot(id);
        r.parent = scene.find(id)->parent();
        r.index = scene.indexInParent(id);
        scene.removeNode(id);
        removed_.push_back(std::move(r));
    }
    return true;
}

void DeleteNodesCommand::revert(Scene& scene) {
    // Reverse order puts every node back at the index it had when removed.
    for (std::size_t i = removed_.size(); i-- > 0;)
        scene.insertSnapshot(removed_[i].snapshot, removed_[i].parent, removed_[i].index);
}

// ---- ReparentCommand ---------------------------------------------------------

namespace {

// Scene::setParent counts its index with the moved node already taken out. The reparent index
// additionally leaves out every other dragged node, because those are not placed yet: they may
// still sit in the new parent's list and would shift the position (the sibling off-by-one).

// Slot of the first dragged node: just before the `cleanIndex`-th child that is not dragged.
int firstSlot(const Scene& scene, NodeId parent, NodeId moving, const std::unordered_set<NodeId>& dragged,
              int cleanIndex) {
    int slot = 0;
    int clean = 0;
    for (NodeId sibling : scene.childrenOf(parent)) {
        if (sibling == moving) continue;
        if (!dragged.count(sibling)) {
            if (clean == cleanIndex) return slot;
            ++clean;
        }
        ++slot;
    }
    return -1;  // fewer clean children than the index: append
}

// Slot right behind `previous`, the dragged node that was placed just before this one.
int slotAfter(const Scene& scene, NodeId parent, NodeId moving, NodeId previous) {
    int slot = 0;
    for (NodeId sibling : scene.childrenOf(parent)) {
        if (sibling == moving) continue;
        ++slot;
        if (sibling == previous) return slot;
    }
    return -1;
}

}  // namespace

ReparentCommand::ReparentCommand(std::vector<NodeId> ids, NodeId newParent, int index, bool keepWorld)
    : ids_(std::move(ids)), newParent_(newParent), index_(index), keepWorld_(keepWorld) {}

bool ReparentCommand::apply(Scene& scene) {
    moved_.clear();
    if (newParent_ != kInvalidNode && !scene.contains(newParent_)) return false;
    const std::vector<NodeId> ids = scene.topLevelOnly(ids_);
    if (ids.empty()) return false;
    for (NodeId id : ids) {
        if (id == newParent_ || scene.isAncestor(id, newParent_)) return false;  // would create a cycle
    }
    const std::unordered_set<NodeId> dragged(ids.begin(), ids.end());
    NodeId previous = kInvalidNode;  // the dragged node placed last; the next one goes behind it
    for (NodeId id : ids) {
        const Node* n = scene.find(id);
        Moved m{id, n->parent(), scene.indexInParent(id), n->transform()};
        const glm::mat4 world = scene.worldMatrix(id);
        int slot = -1;
        if (index_ >= 0)
            slot = previous == kInvalidNode ? firstSlot(scene, newParent_, id, dragged, index_)
                                            : slotAfter(scene, newParent_, id, previous);
        scene.setParent(id, newParent_, slot);
        if (keepWorld_ && m.oldParent != newParent_) scene.setTransform(id, scene.localFromWorld(newParent_, world));
        moved_.push_back(m);
        previous = id;
    }
    return true;
}

void ReparentCommand::revert(Scene& scene) {
    for (std::size_t i = moved_.size(); i-- > 0;) {
        const Moved& m = moved_[i];
        scene.setParent(m.id, m.oldParent, m.oldIndex);
        scene.setTransform(m.id, m.oldTransform);
    }
}

// ---- SetTransformCommand -----------------------------------------------------

SetTransformCommand::SetTransformCommand(NodeId id, const Transform& transform, std::string name)
    : after_{{id, transform}}, name_(std::move(name)) {}

SetTransformCommand::SetTransformCommand(std::vector<std::pair<NodeId, Transform>> targets, std::string name)
    : after_(std::move(targets)), name_(std::move(name)) {}

bool SetTransformCommand::apply(Scene& scene) {
    if (after_.empty()) return false;
    std::vector<Transform> before;
    before.reserve(after_.size());
    for (const auto& entry : after_) {
        const Node* n = scene.find(entry.first);
        if (!n) return false;
        before.push_back(n->transform());
    }
    if (before_.empty()) before_ = std::move(before);
    for (const auto& [id, t] : after_) scene.setTransform(id, t);
    return true;
}

void SetTransformCommand::revert(Scene& scene) {
    for (std::size_t i = 0; i < after_.size() && i < before_.size(); ++i) scene.setTransform(after_[i].first, before_[i]);
}

bool SetTransformCommand::mergeWith(const Command& next) {
    const auto* o = dynamic_cast<const SetTransformCommand*>(&next);
    if (!o || o->after_.size() != after_.size()) return false;
    for (std::size_t i = 0; i < after_.size(); ++i) {
        if (o->after_[i].first != after_[i].first) return false;
    }
    after_ = o->after_;
    return true;
}

// ---- EditNodesCommand --------------------------------------------------------

EditNodesCommand::EditNodesCommand(std::vector<std::pair<NodeId, NodeData>> after, std::string name,
                                   std::string mergeKey)
    : after_(std::move(after)), name_(std::move(name)), mergeKey_(std::move(mergeKey)) {}

bool EditNodesCommand::apply(Scene& scene) {
    if (after_.empty()) return false;
    std::vector<NodeData> before;
    before.reserve(after_.size());
    for (const auto& entry : after_) {
        const Node* n = scene.find(entry.first);
        if (!n) return false;
        before.push_back(n->data());
    }
    if (before_.empty()) before_ = std::move(before);
    for (const auto& [id, d] : after_) scene.setData(id, d);
    return true;
}

void EditNodesCommand::revert(Scene& scene) {
    for (std::size_t i = 0; i < after_.size() && i < before_.size(); ++i) scene.setData(after_[i].first, before_[i]);
}

bool EditNodesCommand::mergeWith(const Command& next) {
    const auto* o = dynamic_cast<const EditNodesCommand*>(&next);
    if (!o || mergeKey_.empty() || o->mergeKey_ != mergeKey_ || o->after_.size() != after_.size()) return false;
    for (std::size_t i = 0; i < after_.size(); ++i) {
        if (o->after_[i].first != after_[i].first) return false;
    }
    after_ = o->after_;
    return true;
}

std::unique_ptr<EditNodesCommand> makeEditCommand(const Scene& scene, const std::vector<NodeId>& ids,
                                                  std::string name, const std::function<void(NodeData&)>& edit,
                                                  std::string mergeKey) {
    std::vector<std::pair<NodeId, NodeData>> after;
    std::unordered_set<NodeId> seen;
    for (NodeId id : ids) {
        const Node* n = scene.find(id);
        if (!n || !seen.insert(id).second) continue;
        NodeData d = n->data();
        edit(d);
        after.emplace_back(id, std::move(d));
    }
    if (after.empty()) return nullptr;
    return std::make_unique<EditNodesCommand>(std::move(after), std::move(name), std::move(mergeKey));
}

// ---- GroupCommand ------------------------------------------------------------

GroupCommand::GroupCommand(std::vector<NodeId> ids, std::string groupName)
    : ids_(std::move(ids)), groupName_(std::move(groupName)) {}

bool GroupCommand::prepare(Scene& scene) {
    const std::vector<NodeId> ids = scene.sortedByTreeOrder(scene.topLevelOnly(ids_));
    if (ids.empty()) return false;
    groupParent_ = scene.find(ids.front())->parent();
    groupIndex_ = scene.indexInParent(ids.front());

    Aabb centres;
    for (NodeId id : ids) centres.expand(glm::vec3(scene.worldMatrix(id)[3]));
    const glm::mat4 parentWorld = groupParent_ == kInvalidNode ? glm::mat4(1.0f) : scene.worldMatrix(groupParent_);
    Transform groupTransform;
    groupTransform.position = glm::vec3(glm::inverse(parentWorld) * glm::vec4(centres.center(), 1.0f));
    group_ = NodeSnapshot{kInvalidNode, groupTransform, makeNodeData(NodeKind::Group, groupName_), {}};

    const glm::mat4 groupWorldInverse = glm::inverse(parentWorld * groupTransform.matrix());
    members_.clear();
    for (NodeId id : ids) {
        Member m;
        m.id = id;
        m.oldTransform = scene.find(id)->transform();
        m.newTransform = Transform::fromMatrix(groupWorldInverse * scene.worldMatrix(id));
        members_.push_back(m);
    }
    return true;
}

bool GroupCommand::apply(Scene& scene) {
    if (!prepared_) {
        if (!prepare(scene)) return false;
        prepared_ = true;
    } else {
        if (groupParent_ != kInvalidNode && !scene.contains(groupParent_)) return false;
        for (const Member& m : members_) {
            if (!scene.contains(m.id)) return false;
        }
    }
    const NodeId gid = scene.insertSnapshot(group_, groupParent_, groupIndex_);
    if (gid == kInvalidNode) return false;
    group_.id = gid;  // keep the id for redo
    for (Member& m : members_) {
        m.oldParent = scene.find(m.id)->parent();
        m.oldIndex = scene.indexInParent(m.id);
        scene.setParent(m.id, gid);
        scene.setTransform(m.id, m.newTransform);
    }
    return true;
}

void GroupCommand::revert(Scene& scene) {
    for (std::size_t i = members_.size(); i-- > 0;) {
        const Member& m = members_[i];
        scene.setParent(m.id, m.oldParent, m.oldIndex);
        scene.setTransform(m.id, m.oldTransform);
    }
    scene.removeNode(group_.id);
}

// ---- UngroupCommand ----------------------------------------------------------

UngroupCommand::UngroupCommand(std::vector<NodeId> groupIds) : ids_(std::move(groupIds)) {}

bool UngroupCommand::apply(Scene& scene) {
    dissolved_.clear();
    std::vector<NodeId> groups;
    for (NodeId id : ids_) {
        const Node* n = scene.find(id);
        if (n && n->kind() == NodeKind::Group && std::find(groups.begin(), groups.end(), id) == groups.end())
            groups.push_back(id);
    }
    if (groups.empty()) return false;
    for (NodeId g : groups) {
        Dissolved d;
        d.parent = scene.find(g)->parent();
        const int groupIndex = scene.indexInParent(g);
        const std::vector<NodeId> children = scene.find(g)->children();
        int k = 0;
        for (NodeId c : children) {
            d.children.push_back({c, scene.find(c)->transform()});
            const glm::mat4 world = scene.worldMatrix(c);
            scene.setParent(c, d.parent, groupIndex + k++);
            scene.setTransform(c, scene.localFromWorld(d.parent, world));
        }
        d.index = scene.indexInParent(g);
        d.group = scene.snapshot(g);
        scene.removeNode(g);
        dissolved_.push_back(std::move(d));
    }
    return true;
}

void UngroupCommand::revert(Scene& scene) {
    for (std::size_t i = dissolved_.size(); i-- > 0;) {
        const Dissolved& d = dissolved_[i];
        scene.insertSnapshot(d.group, d.parent, d.index);
        for (const Child& c : d.children) {
            scene.setParent(c.id, d.group.id);
            scene.setTransform(c.id, c.oldTransform);
        }
    }
}

std::vector<NodeId> UngroupCommand::resultNodes() const {
    std::vector<NodeId> out;
    for (const Dissolved& d : dissolved_) {
        for (const Child& c : d.children) out.push_back(c.id);
    }
    return out;
}

// ---- SetLayersCommand --------------------------------------------------------

SetLayersCommand::SetLayersCommand(std::vector<Layer> layers, std::string name)
    : after_(std::move(layers)), name_(std::move(name)) {}

bool SetLayersCommand::apply(Scene& scene) {
    if (before_.empty()) before_ = scene.layers();
    scene.setLayers(after_);
    return true;
}

void SetLayersCommand::revert(Scene& scene) { scene.setLayers(before_); }

}  // namespace dmxviz::stage
