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

std::string Endpoint::toString() const { return std::format("{}:{}", address.toString(), port); }

}  // namespace dmxviz::dmx
