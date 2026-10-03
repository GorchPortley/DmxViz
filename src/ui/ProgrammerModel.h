#pragma once
// ProgrammerModel: remembers what the test console has put into the DMX manager's
// programmer, channel by channel.
//
// The programmer API can set a channel but cannot take a single channel back. This class
// keeps its own copy of the touched channels, so the console can release one fixture (or
// one fader) and leave the rest alone: it clears the universe's programmer and sends the
// remaining channels again.

#include "dmx/DmxManager.h"

#include <bitset>
#include <cstdint>
#include <map>
#include <vector>

namespace dmxviz::ui {

class ProgrammerModel {
public:
    explicit ProgrammerModel(dmx::DmxManager& manager) : manager_(manager) {}

    // Sets one channel (address 1..512) in the programmer.
    void setChannel(dmx::UniverseId universe, int address, std::uint8_t value);

    // Takes `count` channels from `firstAddress` back out of the programmer.
    void release(dmx::UniverseId universe, int firstAddress, int count);
    // Empties the whole programmer, including values set elsewhere (e.g. --test-pattern).
    void releaseAll();

    bool touched(dmx::UniverseId universe, int address) const;
    // The value last sent for a touched channel, 0 otherwise.
    std::uint8_t value(dmx::UniverseId universe, int address) const;
    bool empty() const { return universes_.empty(); }
    // Universes with at least one touched channel.
    std::vector<dmx::UniverseId> universes() const;

private:
    struct Universe {
        dmx::UniverseData values{};
        std::bitset<dmx::kUniverseSize> touched;
    };

    dmx::DmxManager& manager_;
    std::map<dmx::UniverseId, Universe> universes_;
};

}  // namespace dmxviz::ui
