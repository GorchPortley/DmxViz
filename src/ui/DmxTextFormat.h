#pragma once
// DmxTextFormat: converts lists that the DMX interface panel edits as one line of text -
// universe numbers ("1-4, 7") and unicast targets ("192.168.1.20, 10.0.0.5:6455").
//
// The parse functions leave `out` untouched and return false for invalid text, so the panel
// can colour the field red and keep the last valid setting.

#include "dmx/NetAddress.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dmxviz::ui {

// Sorted universe numbers joined with commas; runs of three or more collapse to "a-b".
std::string formatUniverseList(std::span<const std::uint16_t> universes);

// Accepts numbers and ranges separated by commas or spaces ("1-4, 7"). Every number must lie
// in [minValue, maxValue]. The result is sorted without duplicates. Empty text is valid
// (an empty list).
bool parseUniverseList(std::string_view text, int minValue, int maxValue, std::vector<std::uint16_t>& out);

// "a.b.c.d" or "a.b.c.d:port" entries joined by ", "; the port is left out when it equals the default.
std::string formatEndpointList(std::span<const dmx::Endpoint> endpoints, std::uint16_t defaultPort);
bool parseEndpointList(std::string_view text, std::uint16_t defaultPort, std::vector<dmx::Endpoint>& out);

}  // namespace dmxviz::ui
