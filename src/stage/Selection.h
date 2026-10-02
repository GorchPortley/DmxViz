#pragma once
// The set of selected nodes plus the hovered node.

#include "core/Id.h"
#include "core/SceneTypes.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace dmxviz::stage {

class Scene;

// Selected node ids in the order they were selected. The primary node is the
// one the gizmo and inspector follow; by default the most recently selected.
// Selection is not part of undo history (like most DCC tools).
class Selection {
public:
    const std::vector<NodeId>& ids() const { return ids_; }
    bool empty() const { return ids_.empty(); }
    std::size_t size() const { return ids_.size(); }
    bool contains(NodeId id) const { return set_.count(id) != 0; }
    NodeId primary() const { return primary_; }

    void clear();
    void set(NodeId id);                        // select only this node
    void setMany(const std::vector<NodeId>& ids);  // primary = last
    void add(NodeId id);                        // becomes primary
    void remove(NodeId id);
    void toggle(NodeId id);                     // ctrl+click
    void setPrimary(NodeId id);                 // must already be selected

    NodeId hover() const { return hover_; }
    void setHover(NodeId id);

    // Drops ids that no longer exist in the scene (after delete or undo).
    void prune(const Scene& scene);

    // Highlight of this node alone (Selected wins over Hover).
    Highlight highlightFor(NodeId id) const;

    // Incremented on every change (for UI panels that cache derived data).
    std::uint64_t revision() const { return revision_; }

private:
    std::vector<NodeId> ids_;
    std::unordered_set<NodeId> set_;
    NodeId primary_ = kInvalidNode;
    NodeId hover_ = kInvalidNode;
    std::uint64_t revision_ = 0;
};

}  // namespace dmxviz::stage
