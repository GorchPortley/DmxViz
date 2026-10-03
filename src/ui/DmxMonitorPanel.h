#pragma once
// DmxMonitorPanel: live view of one DMX universe.
//
//   * a universe selector that lists only universes with data (inputs or the test console);
//   * a 32 x 16 grid of the 512 channel values, shaded by intensity, with the first slot
//     and a coloured underline for every patched fixture;
//   * a tooltip per cell: address, value, and which fixture and channel use it (from the patch);
//   * the sources feeding the universe with protocol, sender address, priority and packet rate.
//
// The grid is drawn straight into the window's draw list from the frame's DmxSnapshot, with
// text formatted into stack buffers, so it allocates nothing per frame.

#include "dmx/DmxSnapshot.h"
#include "dmx/DmxTypes.h"
#include "ui/ChannelOwnerMap.h"
#include "ui/Panel.h"

#include "imgui.h"

#include <cstdint>

namespace dmxviz::ui {

class DmxMonitorPanel : public Panel {
public:
    const char* title() const override;
    void draw(EditorContext& ctx) override;

private:
    enum class ValueFormat { Decimal, Percent, Hex };

    void drawToolbar(EditorContext& ctx);
    void drawGrid(EditorContext& ctx, const dmx::UniverseData& values);
    void drawCellTooltip(EditorContext& ctx, int address, std::uint8_t value) const;
    void drawSources(const dmx::UniverseInfo* info);
    void refreshOwners(EditorContext& ctx);

    dmx::UniverseId selected_ = dmx::kInvalidUniverse;
    ValueFormat format_ = ValueFormat::Decimal;

    // Who uses which channel, rebuilt when the scene changes or once in a while.
    ChannelOwnerMap owners_;
    std::uint64_t ownersRevision_ = 0;
    double ownersTime_ = -1.0;
};

}  // namespace dmxviz::ui
