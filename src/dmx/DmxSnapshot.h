#pragma once
// DmxSnapshot: the main thread's private copy of all merged universes for one frame.
//
// The main thread asks DmxManager (or UniverseStore) to fill a snapshot once per frame
// and then reads it without any locking while the IO threads keep receiving. Reusing
// the same snapshot object every frame avoids heap allocations in steady state.

#include "dmx/DmxTypes.h"
#include "dmx/NetAddress.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace dmxviz::dmx {

// One sender contributing to a universe, for the DMX monitor.
struct SourceInfo {
    std::string name;                 // sACN source name, Art-Net/USB interface label, "Programmer"
    Endpoint endpoint;                // remote sender (zero for USB and the programmer)
    Protocol protocol = Protocol::ArtNet;
    std::uint8_t priority = kDefaultPriority;
    InterfaceId interfaceId = 0;      // which configured interface received it
    float packetsPerSecond = 0.0f;
    bool preview = false;             // sACN preview data
    bool held = false;                // timed out; only kept to hold the last look
    TimePoint lastSeen{};
};

struct UniverseInfo {
    UniverseId universe = kInvalidUniverse;
    TimePoint lastUpdate{};           // last time any source sent data
    bool held = false;                // every network source timed out; values are the last look
    std::vector<SourceInfo> sources;  // in arrival order
};

class DmxSnapshot {
public:
    // Merged values of a universe, or nullptr if nothing is known about it.
    const UniverseData* universe(UniverseId id) const;
    // A single channel (address 1..512); 0 for unknown universes or invalid addresses.
    std::uint8_t channel(UniverseId id, std::uint16_t address1Based) const;
    // Universes with data, ascending.
    std::span<const UniverseId> universes() const { return ids_; }
    // Monitor details for a universe, or nullptr.
    const UniverseInfo* info(UniverseId id) const;
    // When the snapshot was taken.
    TimePoint time() const { return time_; }
    bool empty() const { return ids_.empty(); }

private:
    friend class UniverseStore;
    int indexOf(UniverseId id) const;

    std::vector<UniverseId> ids_;
    std::vector<UniverseData> frames_;
    std::vector<UniverseInfo> infos_;
    TimePoint time_{};
};

}  // namespace dmxviz::dmx
