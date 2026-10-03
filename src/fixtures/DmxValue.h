#pragma once
// Reading and writing multi-byte channel values in a fixture's DMX footprint,
// and converting values between channel resolutions (8 / 16 / 24 bit).

#include <cstdint>
#include <span>

namespace dmxviz::fixtures {

constexpr std::uint32_t maxDmxValue(int bytes) {
    return bytes >= 4 ? 0xFFFFFFFFu : (bytes <= 0 ? 0u : (1u << (8 * bytes)) - 1u);
}

// Combines the bytes at the given 1-based offsets (coarse first). Offsets
// beyond the footprint read as 0.
std::uint32_t readDmxValue(std::span<const std::uint8_t> footprint, std::span<const std::uint16_t> offsets);

// Splits value into bytes at the given 1-based offsets (coarse first).
// Offsets beyond the footprint are ignored.
void writeDmxValue(std::span<std::uint8_t> footprint, std::span<const std::uint16_t> offsets, std::uint32_t value);

// Converts a value from one resolution to another. Range *starts* scale by
// shifting (128 @8 bit -> 32768 @16 bit); range *ends* fill the new low bytes
// (127 @8 bit -> 32767 @16 bit) so adjacent ranges stay contiguous.
std::uint32_t convertDmxResolution(std::uint32_t value, int fromBytes, int toBytes, bool isRangeEnd = false);

}  // namespace dmxviz::fixtures
