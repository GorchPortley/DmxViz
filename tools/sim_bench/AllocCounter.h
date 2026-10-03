#pragma once
// Counts every call of the global operator new, so the benchmark can report heap allocations per frame.
// Linking AllocCounter.cpp into an executable replaces operator new/delete for the whole program.

#include <cstdint>

namespace simbench {

struct AllocStats {
    std::uint64_t count = 0;  // number of allocations so far
    std::uint64_t bytes = 0;  // bytes requested so far
};

// Totals since program start; subtract two readings to measure a code region.
AllocStats allocStats();

}  // namespace simbench
