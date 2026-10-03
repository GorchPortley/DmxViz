#include "ui/ProgrammerModel.h"

#include <algorithm>

namespace dmxviz::ui {

void ProgrammerModel::setChannel(dmx::UniverseId universe, int address, std::uint8_t value) {
    if (universe == dmx::kInvalidUniverse || address < 1 || address > static_cast<int>(dmx::kUniverseSize)) return;
    Universe& state = universes_[universe];
    const std::size_t index = static_cast<std::size_t>(address - 1);
    state.touched.set(index);
    state.values[index] = value;
    manager_.setProgrammerChannel(universe, static_cast<std::uint16_t>(address), value);
}

void ProgrammerModel::release(dmx::UniverseId universe, int firstAddress, int count) {
    const auto it = universes_.find(universe);
    if (it == universes_.end()) return;
    Universe& state = it->second;

    const int first = std::max(firstAddress, 1);
    const int last = std::min(firstAddress + count - 1, static_cast<int>(dmx::kUniverseSize));
    bool changed = false;
    for (int address = first; address <= last; ++address) {
        const std::size_t index = static_cast<std::size_t>(address - 1);
        if (!state.touched.test(index)) continue;
        state.touched.reset(index);
        state.values[index] = 0;
        changed = true;
    }
    if (!changed) return;

    // The programmer cannot drop single channels: clear the universe, then send back what is left.
    manager_.clearProgrammerUniverse(universe);
    if (state.touched.none()) {
        universes_.erase(it);
        return;
    }
    for (std::size_t index = 0; index < dmx::kUniverseSize; ++index)
        if (state.touched.test(index))
            manager_.setProgrammerChannel(universe, static_cast<std::uint16_t>(index + 1), state.values[index]);
}

void ProgrammerModel::releaseAll() {
    manager_.clearProgrammer();
    universes_.clear();
}

bool ProgrammerModel::touched(dmx::UniverseId universe, int address) const {
    const auto it = universes_.find(universe);
    if (it == universes_.end() || address < 1 || address > static_cast<int>(dmx::kUniverseSize)) return false;
    return it->second.touched.test(static_cast<std::size_t>(address - 1));
}

std::uint8_t ProgrammerModel::value(dmx::UniverseId universe, int address) const {
    if (!touched(universe, address)) return 0;
    return universes_.at(universe).values[static_cast<std::size_t>(address - 1)];
}

std::vector<dmx::UniverseId> ProgrammerModel::universes() const {
    std::vector<dmx::UniverseId> result;
    for (const auto& entry : universes_) result.push_back(entry.first);
    return result;
}

}  // namespace dmxviz::ui
