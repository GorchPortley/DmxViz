#pragma once
// The concrete undoable scene edits. Construct one and hand it to
// CommandStack::execute(). All of them refer to nodes by id.

#include "stage/Command.h"
#include "stage/Scene.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace dmxviz::stage {

// Several commands as one undo step. If one fails, the ones already applied
// are reverted and the whole compound fails.
class CompoundCommand : public Command {
public:
    explicit CompoundCommand(std::string name, std::vector<std::unique_ptr<Command>> commands = {});
    void add(std::unique_ptr<Command> command) { commands_.push_back(std::move(command)); }
    // Used by CommandStack batches: records a command that was already applied.
    void addApplied(std::unique_ptr<Command> command) { commands_.push_back(std::move(command)); }
    bool empty() const { return commands_.empty(); }
    std::size_t size() const { return commands_.size(); }

    bool apply(Scene& scene) override;
    void revert(Scene& scene) override;
    std::string name() const override { return name_; }
    std::vector<NodeId> resultNodes() const override;

private:
    std::string name_;
    std::vector<std::unique_ptr<Command>> commands_;
};

// ---------------------------------------------------------------------------
// Inserts node subtrees. Snapshot ids of kInvalidNode get fresh ids on the
// first apply; those ids are then reused on every redo.
struct NodeInsertion {
    NodeSnapshot snapshot;
    NodeId parent = kInvalidNode;
    int index = -1;  // < 0 appends
};

class InsertNodesCommand : public Command {
public:
    InsertNodesCommand(std::string name, std::vector<NodeInsertion> insertions);
    bool apply(Scene& scene) override;
    void revert(Scene& scene) override;
    std::string name() const override { return name_; }
    std::vector<NodeId> resultNodes() const override { return created_; }

protected:
    void addInsertion(NodeInsertion insertion) { insertions_.push_back(std::move(insertion)); }

private:
    std::string name_;
    std::vector<NodeInsertion> insertions_;
    std::vector<NodeId> created_;
};

// Creates one node.
class AddNodeCommand : public InsertNodesCommand {
public:
    AddNodeCommand(NodeData data, const Transform& transform = {}, NodeId parent = kInvalidNode, int index = -1);
    // The new node's id (valid after the first successful apply).
    NodeId createdId() const;
};

// Copies of nodes (with their subtrees), each inserted right after its
// original and moved by `worldOffset`. Used by duplicate and paste.
std::unique_ptr<InsertNodesCommand> makeDuplicateCommand(const Scene& scene, const std::vector<NodeId>& ids,
                                                         const glm::vec3& worldOffset = glm::vec3(0.0f));

// Pastes clipboard snapshots (ids are ignored, fresh ones are assigned) as
// children of `parent`.
std::unique_ptr<InsertNodesCommand> makePasteCommand(std::vector<NodeSnapshot> snapshots,
                                                     NodeId parent = kInvalidNode);

// ---------------------------------------------------------------------------
// Deletes nodes with their subtrees. Undo restores ids, parents and sibling order.
class DeleteNodesCommand : public Command {
public:
    explicit DeleteNodesCommand(std::vector<NodeId> ids);
    bool apply(Scene& scene) override;
    void revert(Scene& scene) override;
    std::string name() const override { return "Delete"; }

private:
    struct Removed {
        NodeSnapshot snapshot;
        NodeId parent = kInvalidNode;
        int index = -1;
    };
    std::vector<NodeId> ids_;
    std::vector<Removed> removed_;
};

// ---------------------------------------------------------------------------
// Moves nodes under a new parent (kInvalidNode = top level), optionally at a
// sibling index (counted among the new parent's children that are not being
// moved; -1 appends). keepWorld keeps them where they are in the world by
// adjusting their local transforms (the usual outliner drag behaviour).
class ReparentCommand : public Command {
public:
    ReparentCommand(std::vector<NodeId> ids, NodeId newParent, int index = -1, bool keepWorld = true);
    bool apply(Scene& scene) override;
    void revert(Scene& scene) override;
    std::string name() const override { return "Reparent"; }

private:
    struct Moved {
        NodeId id = kInvalidNode;
        NodeId oldParent = kInvalidNode;
        int oldIndex = -1;
        Transform oldTransform;
    };
    std::vector<NodeId> ids_;
    NodeId newParent_;
    int index_;
    bool keepWorld_;
    std::vector<Moved> moved_;
};

// ---------------------------------------------------------------------------
// Sets local transforms. Consecutive commands for the same nodes merge, so a
// whole gizmo drag is one undo step (call CommandStack::breakMerge() on release).
class SetTransformCommand : public Command {
public:
    SetTransformCommand(NodeId id, const Transform& transform, std::string name = "Transform");
    explicit SetTransformCommand(std::vector<std::pair<NodeId, Transform>> targets, std::string name = "Transform");
    bool apply(Scene& scene) override;
    void revert(Scene& scene) override;
    std::string name() const override { return name_; }
    bool mergeWith(const Command& next) override;

private:
    std::vector<std::pair<NodeId, Transform>> after_;
    std::vector<Transform> before_;
    std::string name_;
};

// ---------------------------------------------------------------------------
// Replaces NodeData (name, visibility, lock, layer, colour, material, content)
// of one or more nodes. Commands with the same non-empty mergeKey and nodes
// merge (slider drags in the inspector).
class EditNodesCommand : public Command {
public:
    EditNodesCommand(std::vector<std::pair<NodeId, NodeData>> after, std::string name, std::string mergeKey = {});
    bool apply(Scene& scene) override;
    void revert(Scene& scene) override;
    std::string name() const override { return name_; }
    bool mergeWith(const Command& next) override;

private:
    std::vector<std::pair<NodeId, NodeData>> after_;
    std::vector<NodeData> before_;
    std::string name_;
    std::string mergeKey_;
};

// Builds an EditNodesCommand by copying each node's current data and letting
// `edit` change it. Returns nullptr when none of the ids exist. Example:
//   stack.execute(makeEditCommand(scene, selection.ids(), "Hide",
//                                 [](NodeData& d) { d.visible = false; }));
std::unique_ptr<EditNodesCommand> makeEditCommand(const Scene& scene, const std::vector<NodeId>& ids,
                                                  std::string name, const std::function<void(NodeData&)>& edit,
                                                  std::string mergeKey = {});

// ---------------------------------------------------------------------------
// Puts nodes into a new Group created under the parent of the first node (in
// tree order), at that node's position. The group's origin is the centre of
// the nodes' positions; world placement of the nodes is unchanged.
class GroupCommand : public Command {
public:
    explicit GroupCommand(std::vector<NodeId> ids, std::string groupName = "Group");
    bool apply(Scene& scene) override;
    void revert(Scene& scene) override;
    std::string name() const override { return "Group"; }
    std::vector<NodeId> resultNodes() const override { return {group_.id}; }
    NodeId groupId() const { return group_.id; }

private:
    struct Member {
        NodeId id = kInvalidNode;
        NodeId oldParent = kInvalidNode;
        int oldIndex = -1;
        Transform oldTransform;
        Transform newTransform;
    };
    bool prepare(Scene& scene);

    std::vector<NodeId> ids_;
    std::string groupName_;
    bool prepared_ = false;
    NodeSnapshot group_;
    NodeId groupParent_ = kInvalidNode;
    int groupIndex_ = -1;
    std::vector<Member> members_;
};

// Dissolves groups: their children move to the group's parent at the group's
// position (keeping world placement) and the empty groups are deleted.
// Non-group ids are ignored.
class UngroupCommand : public Command {
public:
    explicit UngroupCommand(std::vector<NodeId> groupIds);
    bool apply(Scene& scene) override;
    void revert(Scene& scene) override;
    std::string name() const override { return "Ungroup"; }
    std::vector<NodeId> resultNodes() const override;

private:
    struct Child {
        NodeId id = kInvalidNode;
        Transform oldTransform;
    };
    struct Dissolved {
        NodeSnapshot group;  // without children
        NodeId parent = kInvalidNode;
        int index = -1;      // group's index right before it was removed
        std::vector<Child> children;
    };
    std::vector<NodeId> ids_;
    std::vector<Dissolved> dissolved_;
};

// ---------------------------------------------------------------------------
// Replaces the layer table.
class SetLayersCommand : public Command {
public:
    explicit SetLayersCommand(std::vector<Layer> layers, std::string name = "Edit layers");
    bool apply(Scene& scene) override;
    void revert(Scene& scene) override;
    std::string name() const override { return name_; }

private:
    std::vector<Layer> after_;
    std::vector<Layer> before_;
    std::string name_;
};

}  // namespace dmxviz::stage
