#include "fixtures/DmxValue.h"

namespace dmxviz::fixtures {

std::uint32_t readDmxValue(std::span<const std::uint8_t> footprint, std::span<const std::uint16_t> offsets) {
    std::uint32_t value = 0;
    for (std::uint16_t offset : offsets) {
        const std::size_t i = offset >= 1 ? static_cast<std::size_t>(offset - 1) : footprint.size();
        value = (value << 8) | (i < footprint.size() ? footprint[i] : 0u);
    }
    return value;
}

void writeDmxValue(std::span<std::uint8_t> footprint, std::span<const std::uint16_t> offsets, std::uint32_t value) {
    const std::size_t n = offsets.size();
    for (std::size_t k = 0; k < n; ++k) {
        const std::uint16_t offset = offsets[k];
        const std::size_t i = offset >= 1 ? static_cast<std::size_t>(offset - 1) : footprint.size();
        if (i >= footprint.size()) continue;
        const std::size_t shift = 8 * (n - 1 - k);
        footprint[i] = static_cast<std::uint8_t>((value >> shift) & 0xFFu);
    }
}

std::uint32_t convertDmxResolution(std::uint32_t value, int fromBytes, int toBytes, bool isRangeEnd) {
    if (fromBytes == toBytes) return value;
    if (toBytes > fromBytes) {
        const int shift = 8 * (toBytes - fromBytes);
        std::uint32_t v = value << shift;
        if (isRangeEnd) v |= (1u << shift) - 1u;
        return v;
    }
    return value >> (8 * (fromBytes - toBytes));
}

}  // namespace dmxviz::fixtures
