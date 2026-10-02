#pragma once
// Basic DMX512 types shared by the whole DMX module and its users.
//
// OWNER: DMX work stream (WS1).

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

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

// Identifies one configured DmxInterface inside a DmxManager. Ids start at 1 and are
// persisted in the configuration; 0 is reserved for the local programmer (test console).
using InterfaceId = std::uint32_t;
constexpr InterfaceId kProgrammerInterface = 0;

// All DMX timing (source timeouts, packet rates, output refresh) uses a monotonic clock.
// Functions that depend on time take a `now` argument so tests can simulate time
// without sleeping.
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

// Source priorities follow sACN (E1.31): 0..200, default 100. Protocols without a
// priority (Art-Net, USB) use the default.
constexpr std::uint8_t kDefaultPriority = 100;
constexpr std::uint8_t kMaxNetworkPriority = 200;
// The programmer in Override mode sits above anything a network source can claim.
constexpr std::uint8_t kOverridePriority = 201;

// E1.31 "network data loss" timeout; used for every protocol.
constexpr std::chrono::milliseconds kDefaultSourceTimeout{2500};

enum class Protocol : std::uint8_t { ArtNet, Sacn, EnttecPro, OpenDmx, Loopback, Programmer };

constexpr std::string_view protocolName(Protocol protocol) {
    switch (protocol) {
        case Protocol::ArtNet: return "Art-Net";
        case Protocol::Sacn: return "sACN";
        case Protocol::EnttecPro: return "Enttec DMX USB Pro";
        case Protocol::OpenDmx: return "Enttec Open DMX USB";
        case Protocol::Loopback: return "Loopback";
        case Protocol::Programmer: return "Programmer";
    }
    return "?";
}

// How the local programmer (test console) combines with incoming DMX.
enum class ProgrammerMode : std::uint8_t {
    Merge,     // HTP with inputs at the default priority (100)
    Override,  // the channels it has touched win, at priority 201
};

}  // namespace dmxviz::dmx
