#include "dmx/NetAddress.h"

#include <charconv>
#include <format>

namespace dmxviz::dmx {

std::string Ipv4Address::toString() const {
    return std::format("{}.{}.{}.{}", octet(0), octet(1), octet(2), octet(3));
}

std::optional<Ipv4Address> Ipv4Address::parse(std::string_view text) {
    std::uint32_t result = 0;
    const char* p = text.data();
    const char* end = text.data() + text.size();
    for (int part = 0; part < 4; ++part) {
        if (part > 0) {
            if (p == end || *p != '.') return std::nullopt;
            ++p;
        }
        // Each part is 1..3 decimal digits (no sign, no spaces).
        if (p == end || *p < '0' || *p > '9') return std::nullopt;
        unsigned value = 0;
        const auto [next, ec] = std::from_chars(p, end, value);
        if (ec != std::errc() || value > 255 || next - p > 3) return std::nullopt;
        p = next;
        result = (result << 8) | value;
    }
    if (p != end) return std::nullopt;
    return Ipv4Address{result};
}

std::string Endpoint::toString() const {
    return std::format("{}:{}", address.toString(), port);
}

std::optional<Endpoint> Endpoint::parse(std::string_view text, std::uint16_t defaultPort) {
    Endpoint result;
    result.port = defaultPort;
    const std::size_t colon = text.find(':');
    if (colon != std::string_view::npos) {
        const std::string_view portText = text.substr(colon + 1);
        unsigned port = 0;
        const auto [next, ec] = std::from_chars(portText.data(), portText.data() + portText.size(), port);
        if (portText.empty() || ec != std::errc() || next != portText.data() + portText.size() || port > 65535)
            return std::nullopt;
        result.port = static_cast<std::uint16_t>(port);
        text = text.substr(0, colon);
    }
    const auto address = Ipv4Address::parse(text);
    if (!address) return std::nullopt;
    result.address = *address;
    return result;
}

}  // namespace dmxviz::dmx
