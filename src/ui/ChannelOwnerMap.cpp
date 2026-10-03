#include "ui/ChannelOwnerMap.h"

namespace dmxviz::ui {

void ChannelOwnerMap::build(const stage::Scene& scene, const fixtures::FixtureLibrary& library,
                            dmx::UniverseId universe) {
    owners_.fill(ChannelOwner{});
    universe_ = universe;

    for (const NodeId id : scene.nodesOfKind(stage::NodeKind::Fixture)) {
        const stage::Node* node = scene.find(id);
        const stage::FixtureContent* fixture = node != nullptr ? node->as<stage::FixtureContent>() : nullptr;
        if (fixture == nullptr || fixture->patch.universe != universe || fixture->patch.address < 1) continue;
        const fixtures::FixtureType* type = library.find(fixture->fixtureTypeId);
        const fixtures::DmxMode* mode = type != nullptr ? fixtureMode(*type, *fixture) : nullptr;
        if (mode == nullptr) continue;

        const int base = static_cast<int>(fixture->patch.address);  // 1-based
        for (std::size_t c = 0; c < mode->channels.size(); ++c) {
            const std::vector<std::uint16_t>& offsets = mode->channels[c].offsets;
            for (std::size_t b = 0; b < offsets.size(); ++b) {
                const int address = base + static_cast<int>(offsets[b]) - 1;  // offsets are 1-based too
                if (address < 1 || address > kDmxSlots) continue;
                ChannelOwner& owner = owners_[static_cast<std::size_t>(address - 1)];
                if (owner.used()) owner.shared = true;
                owner.fixture = id;
                owner.channel = static_cast<std::int16_t>(c);
                owner.byte = static_cast<std::int8_t>(b);
                owner.first = address == base;
            }
        }
    }
}

const ChannelOwner& ChannelOwnerMap::at(int address) const {
    static const ChannelOwner kNone;
    if (address < 1 || address > kDmxSlots) return kNone;
    return owners_[static_cast<std::size_t>(address - 1)];
}

}  // namespace dmxviz::ui
