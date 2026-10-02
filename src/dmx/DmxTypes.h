#pragma once
// Basic DMX512 types shared by all DMX interfaces.
//
// OWNER: DMX work stream (WS1).

#include <array>
#include <cstddef>
#include <cstdint>

namespace dmxviz::dmx {

// Logical universe number as shown to the user. 1-based; 0 is invalid.
// Each interface maps its protocol's own numbering onto this (e.g. Art-Net
// port-address 0 -> universe 1 by default, sACN universe N -> N).
using UniverseId = std::uint16_t;
constexpr UniverseId kInvalidUniverse = 0;

constexpr std::size_t kUniverseSize = 512;
using UniverseData = std::array<std::uint8_t, kUniverseSize>;

// A 1-based DMX address (1..512) within a universe.
struct DmxAddress {
    UniverseId universe = 1;
    std::uint16_t address = 1;
    bool operator==(const DmxAddress&) const = default;
};

}  // namespace dmxviz::dmx
