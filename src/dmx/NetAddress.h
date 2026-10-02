#pragma once
// IPv4 address and UDP endpoint value types.
//
// Platform independent on purpose: protocol code, the universe store and the UI use
// these, and only the socket wrapper converts them to sockaddr_in.

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace dmxviz::dmx {

// An IPv4 address in host byte order (192.168.1.20 is 0xC0A80114).
struct Ipv4Address {
    std::uint32_t value = 0;

    static constexpr Ipv4Address fromOctets(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) {
        return Ipv4Address{(std::uint32_t{a} << 24) | (std::uint32_t{b} << 16) | (std::uint32_t{c} << 8) |
                           std::uint32_t{d}};
    }
    // octet(0) is the first (most significant) part of the dotted notation.
    constexpr std::uint8_t octet(int index) const {
        return static_cast<std::uint8_t>((value >> (24 - 8 * index)) & 0xFFu);
    }
    constexpr bool isAny() const { return value == 0; }
    constexpr bool isLoopback() const { return (value >> 24) == 127; }
    constexpr bool isMulticast() const { return (value >> 28) == 0xE; }

    // Dotted notation, e.g. "192.168.1.20".
    std::string toString() const;
    // Parses strict dotted-quad notation; returns nullopt for anything else.
    static std::optional<Ipv4Address> parse(std::string_view text);

    auto operator<=>(const Ipv4Address&) const = default;
};

constexpr Ipv4Address kAnyAddress{0};
constexpr Ipv4Address kLoopbackAddress{0x7F000001u};
constexpr Ipv4Address kLimitedBroadcast{0xFFFFFFFFu};

// An IPv4 address plus UDP port.
struct Endpoint {
    Ipv4Address address;
    std::uint16_t port = 0;

    std::string toString() const;  // "192.168.1.20:6454"
    // Parses "a.b.c.d" (port = defaultPort) or "a.b.c.d:port".
    static std::optional<Endpoint> parse(std::string_view text, std::uint16_t defaultPort);
    auto operator<=>(const Endpoint&) const = default;
};

}  // namespace dmxviz::dmx
