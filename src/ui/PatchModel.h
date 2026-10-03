#pragma once
// PatchModel: the DMX patch bookkeeping shared by the fixture library (auto-patch
// new fixtures), the patch panel (conflicts, auto-patch) and the DMX monitor.
//
//   PatchAllocator used(scene, library);
//   DmxPatch free = used.findFree(footprint);      // first block of free slots
//
// No ImGui in here on purpose: it is plain logic and has unit tests.

#include "core/Id.h"
#include "fixtures/FixtureLibrary.h"
#include "stage/NodeContent.h"
#include "stage/Scene.h"

#include <bitset>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace dmxviz::ui {

inline constexpr int kDmxSlots = 512;                 // channels per universe
inline constexpr std::uint32_t kMaxUniverse = 63999;  // highest sACN universe number

// The mode of a fixture node: the named one, or the type's first mode as a fallback.
// Null when the type has no modes.
const fixtures::DmxMode* fixtureMode(const fixtures::FixtureType& type, const stage::FixtureContent& fixture);

// Number of DMX slots the fixture's current mode uses; 0 when its type or mode is unknown.
int fixtureFootprint(const fixtures::FixtureLibrary& library, const stage::FixtureContent& fixture);

// Remembers which slots of which universes are taken, and finds free blocks.
class PatchAllocator {
public:
    PatchAllocator() = default;
    // Marks the slots of every patched fixture in `scene` as used, except the nodes in `ignore`
    // (e.g. the fixtures that are about to be re-patched).
    PatchAllocator(const stage::Scene& scene, const fixtures::FixtureLibrary& library,
                   std::span<const NodeId> ignore = {});

    // True if `footprint` slots from `address` (1-based) fit into the universe and are all unused.
    bool isFree(std::uint32_t universe, int address, int footprint) const;
    void reserve(std::uint32_t universe, int address, int footprint);
    // The first free block of `footprint` slots at or after `start`; an unpatched result
    // (universe 0) means nothing fits below kMaxUniverse.
    stage::DmxPatch findFree(int footprint, stage::DmxPatch start = {1, 1}) const;

private:
    std::map<std::uint32_t, std::bitset<kDmxSlots>> used_;
};

// ---------------------------------------------------------------------------

// One patched fixture, as the conflict check needs it.
struct PatchSlot {
    stage::DmxPatch patch;
    int footprint = 0;

    // Last slot used (1-based address of the final channel).
    int endAddress() const { return static_cast<int>(patch.address) + footprint - 1; }
    bool patched() const { return patch.patched() && footprint > 0; }
};

enum class PatchProblem : std::uint8_t {
    None,
    Overlap,   // shares slots with another fixture
    Overflow,  // runs past slot 512
};

// Fills `problems` (one entry per slot, same order). `order` is scratch space the caller
// keeps between frames so the check does not allocate in steady state.
void findPatchProblems(std::span<const PatchSlot> slots, std::vector<PatchProblem>& problems,
                       std::vector<int>& order);

// True when both are patched into the same universe and share at least one slot.
bool patchesOverlap(const PatchSlot& a, const PatchSlot& b);

// Consecutive patch addresses for fixtures with the given footprints, starting at `start`.
// A fixture that would cross slot 512 moves to the start of the next universe. Blocks
// already taken in `occupied` are skipped (pass an empty allocator to ignore other
// fixtures). Footprints of 0 (unknown types) stay unpatched.
std::vector<stage::DmxPatch> planAutoPatch(std::span<const int> footprints, stage::DmxPatch start,
                                           PatchAllocator occupied = {});

}  // namespace dmxviz::ui
