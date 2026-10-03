#pragma once
// FixtureSpawner: puts new fixtures into the scene. Used by the fixture library
// ("Add to scene" button) and by the viewport (drag a fixture type onto the stage).
//
// Every call is one undo step. The new fixture gets the next free DMX address
// (first block of free slots that fits its footprint), the next fixture number,
// and is selected afterwards.

#include "core/Id.h"
#include "stage/Picking.h"
#include "ui/EditorContext.h"

#include <string_view>

namespace dmxviz::ui {

class FixtureSpawner {
public:
    // Drops a fixture at a picked point: onto a truss it hangs from the truss chord
    // (like the "hang on truss" tool), on any other surface it stands on it with its
    // beam pointing away from the surface. Returns the new node, kInvalidNode on failure.
    static NodeId addAtHit(EditorContext& ctx, std::string_view typeId, std::string_view modeName,
                           const stage::PickHit& hit);

    // "Add to scene" without a pointer: hangs on the selected truss, otherwise appears
    // above the middle of the stage. Positions already taken by a fixture are skipped.
    static NodeId addNearSelection(EditorContext& ctx, std::string_view typeId, std::string_view modeName);

private:
    static NodeId create(EditorContext& ctx, std::string_view typeId, std::string_view modeName,
                         const stage::Transform& placement, NodeId trussToHangOn, const glm::vec3& hangPoint);
};

}  // namespace dmxviz::ui
