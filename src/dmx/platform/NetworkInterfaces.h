#pragma once
// Lists the machine's IPv4 network interfaces so the user can pick the NIC an
// Art-Net or sACN interface binds to (getifaddrs on Linux, GetAdaptersAddresses on
// Windows).

#include "dmx/NetAddress.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dmxviz::dmx {

// One IPv4 address of one network adapter (an adapter with two addresses appears twice).
struct NetworkInterfaceInfo {
    std::string name;  // "eth0", "Ethernet 2", ...
    Ipv4Address address;
    Ipv4Address netmask;
    Ipv4Address broadcast;              // directed broadcast, e.g. 192.168.1.255
    std::array<std::uint8_t, 6> mac{};  // all zero if unknown
    bool loopback = false;
    bool up = false;

    // True if `other` is on this interface's subnet.
    bool sameSubnet(Ipv4Address other) const {
        return (other.value & netmask.value) == (address.value & netmask.value);
    }
    // "eth0 (192.168.1.10)" for drop-down lists.
    std::string label() const;
};

std::vector<NetworkInterfaceInfo> listNetworkInterfaces();

// The interface that owns `address`, if any.
std::optional<NetworkInterfaceInfo> findNetworkInterface(Ipv4Address address);

}  // namespace dmxviz::dmx
