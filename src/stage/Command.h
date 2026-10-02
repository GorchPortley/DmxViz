#pragma once
// Base class of all undoable scene edits (FR-STG-6).

#include "core/Id.h"

#include <string>
#include <vector>

namespace dmxviz::stage {

class Scene;

// One undoable edit. Commands refer to nodes by id (never by pointer), so
// they stay valid while other commands delete and re-create nodes.
//
// Life cycle: CommandStack::execute() calls apply(); undo calls revert();
// redo calls apply() again. apply() must therefore produce the same result
// every time it runs on the same scene state, e.g. re-create deleted nodes
// with their original ids.
class Command {
public:
    virtual ~Command() = default;

    // Performs the edit. Returns false (leaving the scene unchanged) when it
    // cannot be applied, e.g. because a node no longer exists.
    virtual bool apply(Scene& scene) = 0;
    // Undoes a successful apply().
    virtual void revert(Scene& scene) = 0;
    // Shown in the Edit menu ("Undo Move").
    virtual std::string name() const = 0;

    // `next` was just executed after this command. Return true to absorb it, so
    // that both become one undo step (e.g. the many small steps of a gizmo
    // drag). `next` has already been applied to the scene.
    virtual bool mergeWith(const Command& next) {
        (void)next;
        return false;
    }

    // Nodes the UI should select after executing this command (created
    // nodes, the new group...). Empty means "keep the selection".
    virtual std::vector<NodeId> resultNodes() const { return {}; }
};

}  // namespace dmxviz::stage
