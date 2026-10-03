#pragma once
// Writes a busy light show into the DMX programmer every frame: moving pan/tilt, colour fades (per pixel on
// pixel bars), zoom, gobo and prism changes, and strobe bursts. Everything changes every frame, so the
// simulation cannot get away with caching decoded values.

#include "RigBuilder.h"
#include "dmx/UniverseStore.h"
#include "fixtures/AttributeEncoder.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace simbench {

class DmxAnimator {
public:
    DmxAnimator(const std::vector<RigFixture>& fixtures, int universes);

    // Computes all fixtures' DMX at `timeSeconds` and hands every universe to the store's programmer.
    void animate(double timeSeconds, dmxviz::dmx::UniverseStore& store);

private:
    // One fixture's encoder plus what it can do (looked up once, not per frame).
    struct Look {
        RigFixture fixture;
        std::unique_ptr<dmxviz::fixtures::AttributeEncoder> encoder;
        std::vector<std::uint8_t> bytes;
        std::vector<std::string_view> colorGeometries;  // one entry per pixel/cell (or one for the whole fixture)
        int gobo1Slots = 0, gobo2Slots = 0, prismSlots = 0;
        float phase = 0.0f;  // spreads the fixtures out so they do not all do the same thing
        bool pan = false, tilt = false, zoom = false, dimmer = false, gobo1Rotate = false;
    };

    void animateOne(Look& look, int index, double t) const;

    std::vector<Look> looks_;
    std::vector<dmxviz::dmx::UniverseData> universes_;  // universe number - 1
};

}  // namespace simbench
