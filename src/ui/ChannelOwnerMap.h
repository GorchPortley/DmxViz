#pragma once
// ChannelOwnerMap: for one universe, which fixture (and which of its DMX channels) uses
// each of the 512 slots, built from the patch. The DMX monitor shows it in its tooltips
// and colours; tests check it without any UI.

#include "core/Id.h"
#include "dmx/DmxTypes.h"
#include "fixtures/FixtureLibrary.h"
#include "stage/Scene.h"
#include "ui/PatchModel.h"

#include <array>
#include <cstdint>

namespace dmxviz::ui {

struct ChannelOwner {
    NodeId fixture = kInvalidNode;
    std::int16_t channel = -1;  // index into DmxMode::channels
    std::int8_t byte = 0;       // 0 = coarse byte, 1 = fine byte, ...
    bool shared = false;        // another fixture claims the same slot (a patch conflict)
    bool first = false;         // first slot of the fixture's footprint

    bool used() const { return fixture != kInvalidNode; }
};

class ChannelOwnerMap {
public:
    // Fills the map with every fixture patched into `universe`. Unknown fixture types and
    // slots beyond 512 are skipped.
    void build(const stage::Scene& scene, const fixtures::FixtureLibrary& library, dmx::UniverseId universe);

    // `address` is 1..512; anything else gives an unused entry.
    const ChannelOwner& at(int address) const;
    dmx::UniverseId universe() const { return universe_; }

private:
    std::array<ChannelOwner, kDmxSlots> owners_{};
    dmx::UniverseId universe_ = dmx::kInvalidUniverse;
};

}  // namespace dmxviz::ui
