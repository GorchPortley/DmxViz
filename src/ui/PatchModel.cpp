#include "ui/PatchModel.h"

#include <algorithm>
#include <numeric>

namespace dmxviz::ui {

const fixtures::DmxMode* fixtureMode(const fixtures::FixtureType& type, const stage::FixtureContent& fixture) {
    if (const fixtures::DmxMode* mode = type.findMode(fixture.modeName)) return mode;
    return type.modes.empty() ? nullptr : &type.modes.front();
}

int fixtureFootprint(const fixtures::FixtureLibrary& library, const stage::FixtureContent& fixture) {
    const fixtures::FixtureType* type = library.find(fixture.fixtureTypeId);
    if (type == nullptr) return 0;
    const fixtures::DmxMode* mode = fixtureMode(*type, fixture);
    return mode != nullptr ? mode->footprint : 0;
}

// ---------------------------------------------------------------------------
// PatchAllocator

PatchAllocator::PatchAllocator(const stage::Scene& scene, const fixtures::FixtureLibrary& library,
                               std::span<const NodeId> ignore) {
    for (const NodeId id : scene.nodesOfKind(stage::NodeKind::Fixture)) {
        if (std::find(ignore.begin(), ignore.end(), id) != ignore.end()) continue;
        const stage::Node* node = scene.find(id);
        const stage::FixtureContent* fixture = node != nullptr ? node->as<stage::FixtureContent>() : nullptr;
        if (fixture == nullptr || !fixture->patch.patched()) continue;
        reserve(fixture->patch.universe, static_cast<int>(fixture->patch.address),
                fixtureFootprint(library, *fixture));
    }
}

bool PatchAllocator::isFree(std::uint32_t universe, int address, int footprint) const {
    if (address < 1 || footprint < 1 || address + footprint - 1 > kDmxSlots) return false;
    const auto it = used_.find(universe);
    if (it == used_.end()) return true;
    for (int i = 0; i < footprint; ++i)
        if (it->second.test(static_cast<std::size_t>(address - 1 + i))) return false;
    return true;
}

void PatchAllocator::reserve(std::uint32_t universe, int address, int footprint) {
    if (universe == 0 || address < 1) return;
    std::bitset<kDmxSlots>& bits = used_[universe];
    const int last = std::min(address + footprint - 1, kDmxSlots);
    for (int a = address; a <= last; ++a) bits.set(static_cast<std::size_t>(a - 1));
}

stage::DmxPatch PatchAllocator::findFree(int footprint, stage::DmxPatch start) const {
    if (footprint < 1 || footprint > kDmxSlots) return {};
    std::uint32_t universe = std::max<std::uint32_t>(start.universe, 1);
    int address = start.address >= 1 ? static_cast<int>(start.address) : 1;
    for (; universe <= kMaxUniverse; ++universe, address = 1) {
        for (; address + footprint - 1 <= kDmxSlots; ++address)
            if (isFree(universe, address, footprint)) return {universe, static_cast<std::uint32_t>(address)};
    }
    return {};
}

// ---------------------------------------------------------------------------
// Conflicts

bool patchesOverlap(const PatchSlot& a, const PatchSlot& b) {
    if (!a.patched() || !b.patched() || a.patch.universe != b.patch.universe) return false;
    return a.patch.address <= static_cast<std::uint32_t>(b.endAddress()) &&
           b.patch.address <= static_cast<std::uint32_t>(a.endAddress());
}

void findPatchProblems(std::span<const PatchSlot> slots, std::vector<PatchProblem>& problems,
                       std::vector<int>& order) {
    problems.assign(slots.size(), PatchProblem::None);

    order.clear();
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (!slots[i].patched()) continue;
        order.push_back(static_cast<int>(i));
        if (slots[i].endAddress() > kDmxSlots) problems[i] = PatchProblem::Overflow;
    }
    std::sort(order.begin(), order.end(), [&slots](int a, int b) {
        const PatchSlot& x = slots[static_cast<std::size_t>(a)];
        const PatchSlot& y = slots[static_cast<std::size_t>(b)];
        if (x.patch.universe != y.patch.universe) return x.patch.universe < y.patch.universe;
        return x.patch.address < y.patch.address;
    });

    // Sweep each universe in address order. The slot that reaches furthest so far is the
    // only one a later slot can overlap without also overlapping a nearer one.
    std::uint32_t universe = 0;
    int reachIndex = -1;
    int reachEnd = 0;
    for (const int index : order) {
        const PatchSlot& slot = slots[static_cast<std::size_t>(index)];
        if (slot.patch.universe != universe) {
            universe = slot.patch.universe;
            reachIndex = -1;
            reachEnd = 0;
        }
        if (reachIndex >= 0 && static_cast<int>(slot.patch.address) <= reachEnd) {
            problems[static_cast<std::size_t>(index)] = PatchProblem::Overlap;
            problems[static_cast<std::size_t>(reachIndex)] = PatchProblem::Overlap;
        }
        if (reachIndex < 0 || slot.endAddress() > reachEnd) {
            reachIndex = index;
            reachEnd = slot.endAddress();
        }
    }
}

// ---------------------------------------------------------------------------
// Auto-patch

std::vector<stage::DmxPatch> planAutoPatch(std::span<const int> footprints, stage::DmxPatch start,
                                           PatchAllocator occupied) {
    std::vector<stage::DmxPatch> result(footprints.size());
    stage::DmxPatch cursor{std::max<std::uint32_t>(start.universe, 1), std::max<std::uint32_t>(start.address, 1)};
    for (std::size_t i = 0; i < footprints.size(); ++i) {
        if (footprints[i] < 1) continue;
        const stage::DmxPatch found = occupied.findFree(footprints[i], cursor);
        if (!found.patched()) break;  // out of universes: leave the rest unpatched
        result[i] = found;
        occupied.reserve(found.universe, static_cast<int>(found.address), footprints[i]);
        cursor = {found.universe, found.address + static_cast<std::uint32_t>(footprints[i])};
        if (cursor.address > static_cast<std::uint32_t>(kDmxSlots)) cursor = {found.universe + 1, 1};
    }
    return result;
}

}  // namespace dmxviz::ui
