#include "stage/Selection.h"

#include "stage/Scene.h"

#include <algorithm>

namespace dmxviz::stage {

void Selection::clear() {
    if (ids_.empty()) return;
    ids_.clear();
    set_.clear();
    primary_ = kInvalidNode;
    ++revision_;
}

void Selection::set(NodeId id) {
    ids_.clear();
    set_.clear();
    primary_ = kInvalidNode;
    add(id);
    ++revision_;
}

void Selection::setMany(const std::vector<NodeId>& ids) {
    ids_.clear();
    set_.clear();
    primary_ = kInvalidNode;
    for (NodeId id : ids) add(id);
    ++revision_;
}

void Selection::add(NodeId id) {
    if (id == kInvalidNode) return;
    if (set_.insert(id).second) ids_.push_back(id);
    primary_ = id;
    ++revision_;
}

void Selection::remove(NodeId id) {
    if (set_.erase(id) == 0) return;
    ids_.erase(std::remove(ids_.begin(), ids_.end(), id), ids_.end());
    if (primary_ == id) primary_ = ids_.empty() ? kInvalidNode : ids_.back();
    ++revision_;
}

void Selection::toggle(NodeId id) {
    if (contains(id))
        remove(id);
    else
        add(id);
}

void Selection::setPrimary(NodeId id) {
    if (!contains(id)) return;
    primary_ = id;
    ++revision_;
}

void Selection::setHover(NodeId id) {
    if (hover_ == id) return;
    hover_ = id;
    ++revision_;
}

void Selection::prune(const Scene& scene) {
    std::vector<NodeId> keep;
    keep.reserve(ids_.size());
    for (NodeId id : ids_) {
        if (scene.contains(id)) keep.push_back(id);
    }
    const bool hoverGone = hover_ != kInvalidNode && !scene.contains(hover_);
    if (keep.size() == ids_.size() && !hoverGone) return;
    const NodeId oldPrimary = primary_;
    setMany(keep);
    if (contains(oldPrimary)) primary_ = oldPrimary;
    if (hoverGone) hover_ = kInvalidNode;
    ++revision_;
}

Highlight Selection::highlightFor(NodeId id) const {
    if (id == kInvalidNode) return Highlight::None;
    if (contains(id)) return Highlight::Selected;
    if (hover_ == id) return Highlight::Hover;
    return Highlight::None;
}

}  // namespace dmxviz::stage
