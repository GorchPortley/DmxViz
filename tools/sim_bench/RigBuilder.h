#pragma once
// Builds the benchmark rig: a stage with truss lines and N fixtures of every bundled fixture type, spread
// over many DMX universes (the NFR-1 reference rig is 500 fixtures, ~1000 beams incl. pixel bars, 64 universes).

#include "fixtures/FixtureLibrary.h"
#include "stage/Scene.h"

#include <string>
#include <vector>

namespace simbench {

struct RigOptions {
    int fixtures = 500;
    int universes = 64;
};

// One fixture of the rig, as the animator needs it.
struct RigFixture {
    dmxviz::NodeId node = dmxviz::kInvalidNode;
    const dmxviz::fixtures::FixtureType* type = nullptr;
    const dmxviz::fixtures::DmxMode* mode = nullptr;
    dmxviz::stage::DmxPatch patch;
};

// Fills `scene` (cleared first) and `out`. Returns false with a message when a bundled fixture type is missing.
bool buildRig(dmxviz::stage::Scene& scene, const dmxviz::fixtures::FixtureLibrary& library, const RigOptions& options,
              std::vector<RigFixture>& out, std::string& error);

}  // namespace simbench
