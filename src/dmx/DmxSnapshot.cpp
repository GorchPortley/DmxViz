#include "dmx/DmxSnapshot.h"

#include <algorithm>

namespace dmxviz::dmx {

int DmxSnapshot::indexOf(UniverseId id) const {
    // ids_ is sorted; a binary search over a few dozen universes is plenty fast.
    const auto it = std::lower_bound(ids_.begin(), ids_.end(), id);
    if (it == ids_.end() || *it != id) return -1;
    return static_cast<int>(it - ids_.begin());
}

const UniverseData* DmxSnapshot::universe(UniverseId id) const {
    const int index = indexOf(id);
    return index < 0 ? nullptr : &frames_[static_cast<std::size_t>(index)];
}

std::uint8_t DmxSnapshot::channel(UniverseId id, std::uint16_t address1Based) const {
    if (address1Based < 1 || address1Based > kUniverseSize) return 0;
    const UniverseData* data = universe(id);
    return data ? (*data)[address1Based - 1u] : std::uint8_t{0};
}

const UniverseInfo* DmxSnapshot::info(UniverseId id) const {
    const int index = indexOf(id);
    return index < 0 ? nullptr : &infos_[static_cast<std::size_t>(index)];
}

}  // namespace dmxviz::dmx
