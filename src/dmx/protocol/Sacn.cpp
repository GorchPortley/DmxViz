#include "dmx/protocol/Sacn.h"

#include "dmx/protocol/ByteIo.h"

#include <algorithm>
#include <random>

namespace dmxviz::dmx::sacn {
namespace {

constexpr std::array<std::uint8_t, 12> kAcnPacketId = {0x41, 0x53, 0x43, 0x2D, 0x45, 0x31,
                                                       0x2E, 0x31, 0x37, 0x00, 0x00, 0x00};  // "ASC-E1.17"

constexpr std::uint32_t kVectorFramingData = 0x00000002;
constexpr std::uint8_t kVectorDmpSetProperty = 0x02;
constexpr std::uint8_t kDmpAddressAndDataType = 0xA1;
constexpr std::uint16_t kFlags = 0x7000;  // high nibble of every flags+length field

// Byte offsets of the data packet fields (E1.31-2018 table 4-1).
constexpr std::size_t kPreambleSize = 0;
constexpr std::size_t kPostambleSize = 2;
constexpr std::size_t kAcnId = 4;
constexpr std::size_t kRootFlagsLength = 16;
constexpr std::size_t kRootVector = 18;
constexpr std::size_t kCid = 22;
constexpr std::size_t kFramingFlagsLength = 38;
constexpr std::size_t kFramingVector = 40;
constexpr std::size_t kSourceName = 44;
constexpr std::size_t kPriority = 108;
constexpr std::size_t kSyncAddress = 109;
constexpr std::size_t kSequence = 111;
constexpr std::size_t kOptions = 112;
constexpr std::size_t kUniverse = 113;
constexpr std::size_t kDmpFlagsLength = 115;
constexpr std::size_t kDmpVector = 117;
constexpr std::size_t kDmpAddressType = 118;
constexpr std::size_t kFirstPropertyAddress = 119;
constexpr std::size_t kAddressIncrement = 121;
constexpr std::size_t kPropertyValueCount = 123;
constexpr std::size_t kStartCode = 125;

constexpr std::uint8_t kOptionPreview = 0x80;
constexpr std::uint8_t kOptionTerminated = 0x40;
constexpr std::uint8_t kOptionForceSync = 0x20;

// Reads a flags+length field; returns the PDU length or nullopt if the flags are not 0x7.
std::optional<std::size_t> pduLength(const std::uint8_t* p) {
    const std::uint16_t v = bytes::readU16BE(p);
    if ((v & 0xF000) != kFlags) return std::nullopt;
    return static_cast<std::size_t>(v & 0x0FFF);
}

void writePduLength(std::uint8_t* p, std::size_t length) {
    bytes::writeU16BE(p, static_cast<std::uint16_t>(kFlags | (length & 0x0FFF)));
}

// Preamble, post-amble and ACN identifier: identical for every E1.31 packet.
bool hasValidPreamble(std::span<const std::uint8_t> packet) {
    if (packet.size() < kRootVector + 4) return false;
    if (bytes::readU16BE(&packet[kPreambleSize]) != 0x0010) return false;
    if (bytes::readU16BE(&packet[kPostambleSize]) != 0x0000) return false;
    return std::equal(kAcnPacketId.begin(), kAcnPacketId.end(), packet.begin() + kAcnId);
}

}  // namespace

std::size_t encodeData(const DataPacket& packet, std::span<std::uint8_t> out) {
    if (packet.slots.size() > 512) return 0;
    if (packet.universe < kMinUniverse || packet.universe > kMaxUniverse) return 0;
    const std::size_t total = kDataHeaderSize + packet.slots.size();
    if (out.size() < total) return 0;

    std::fill(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(kDataHeaderSize), std::uint8_t{0});
    // Root layer
    bytes::writeU16BE(&out[kPreambleSize], 0x0010);
    bytes::writeU16BE(&out[kPostambleSize], 0x0000);
    std::copy(kAcnPacketId.begin(), kAcnPacketId.end(), out.begin() + kAcnId);
    writePduLength(&out[kRootFlagsLength], total - kRootFlagsLength);
    bytes::writeU32BE(&out[kRootVector], kVectorRootData);
    std::copy(packet.cid.begin(), packet.cid.end(), out.begin() + kCid);
    // Framing layer
    writePduLength(&out[kFramingFlagsLength], total - kFramingFlagsLength);
    bytes::writeU32BE(&out[kFramingVector], kVectorFramingData);
    const std::size_t nameLength = std::min(packet.sourceName.size(), kSourceNameSize - 1);  // keep a NUL
    std::copy_n(packet.sourceName.begin(), nameLength, out.begin() + kSourceName);  // memcpy(nullptr) is UB
    out[kPriority] = std::min(packet.priority, kMaxPriority);
    bytes::writeU16BE(&out[kSyncAddress], packet.syncAddress);
    out[kSequence] = packet.sequence;
    out[kOptions] = static_cast<std::uint8_t>((packet.preview ? kOptionPreview : 0) |
                                              (packet.streamTerminated ? kOptionTerminated : 0) |
                                              (packet.forceSync ? kOptionForceSync : 0));
    bytes::writeU16BE(&out[kUniverse], packet.universe);
    // DMP layer
    writePduLength(&out[kDmpFlagsLength], total - kDmpFlagsLength);
    out[kDmpVector] = kVectorDmpSetProperty;
    out[kDmpAddressType] = kDmpAddressAndDataType;
    bytes::writeU16BE(&out[kFirstPropertyAddress], 0x0000);
    bytes::writeU16BE(&out[kAddressIncrement], 0x0001);
    bytes::writeU16BE(&out[kPropertyValueCount], static_cast<std::uint16_t>(1 + packet.slots.size()));
    out[kStartCode] = packet.startCode;
    std::copy(packet.slots.begin(), packet.slots.end(), out.begin() + kDataHeaderSize);
    return total;
}

std::optional<DataPacket> decodeData(std::span<const std::uint8_t> packet) {
    if (packet.size() < kDataHeaderSize || !hasValidPreamble(packet)) return std::nullopt;

    // Root layer: the PDU covers the rest of the packet (extra trailing bytes are tolerated).
    const auto rootLength = pduLength(&packet[kRootFlagsLength]);
    if (!rootLength || kRootFlagsLength + *rootLength > packet.size()) return std::nullopt;
    if (bytes::readU32BE(&packet[kRootVector]) != kVectorRootData) return std::nullopt;

    // Framing layer: must end exactly where the root PDU ends.
    const auto framingLength = pduLength(&packet[kFramingFlagsLength]);
    if (!framingLength || kFramingFlagsLength + *framingLength != kRootFlagsLength + *rootLength) return std::nullopt;
    if (bytes::readU32BE(&packet[kFramingVector]) != kVectorFramingData) return std::nullopt;

    // DMP layer: fixed addressing (first address 0, increment 1), 1..513 property values.
    const auto dmpLength = pduLength(&packet[kDmpFlagsLength]);
    if (!dmpLength || kDmpFlagsLength + *dmpLength != kFramingFlagsLength + *framingLength) return std::nullopt;
    if (packet[kDmpVector] != kVectorDmpSetProperty || packet[kDmpAddressType] != kDmpAddressAndDataType)
        return std::nullopt;
    if (bytes::readU16BE(&packet[kFirstPropertyAddress]) != 0 || bytes::readU16BE(&packet[kAddressIncrement]) != 1)
        return std::nullopt;
    const std::size_t valueCount = bytes::readU16BE(&packet[kPropertyValueCount]);
    if (valueCount < 1 || valueCount > 513 || kStartCode + valueCount != kDmpFlagsLength + *dmpLength)
        return std::nullopt;

    DataPacket result;
    result.universe = bytes::readU16BE(&packet[kUniverse]);
    if (result.universe < kMinUniverse || result.universe > kMaxUniverse) return std::nullopt;

    std::copy_n(packet.begin() + kCid, result.cid.size(), result.cid.begin());
    // The name is NUL-terminated UTF-8; tolerate a sender that fills all 64 bytes.
    const char* name = reinterpret_cast<const char*>(&packet[kSourceName]);
    const char* nameEnd = std::find(name, name + kSourceNameSize, '\0');
    result.sourceName = std::string_view(name, static_cast<std::size_t>(nameEnd - name));
    result.priority = std::min(packet[kPriority], kMaxPriority);
    result.syncAddress = bytes::readU16BE(&packet[kSyncAddress]);
    result.sequence = packet[kSequence];
    const std::uint8_t options = packet[kOptions];
    result.preview = (options & kOptionPreview) != 0;
    result.streamTerminated = (options & kOptionTerminated) != 0;
    result.forceSync = (options & kOptionForceSync) != 0;
    result.startCode = packet[kStartCode];
    result.slots = packet.subspan(kDataHeaderSize, valueCount - 1);
    return result;
}

bool isExtendedPacket(std::span<const std::uint8_t> packet) {
    if (!hasValidPreamble(packet)) return false;
    const auto rootLength = pduLength(&packet[kRootFlagsLength]);
    return rootLength && bytes::readU32BE(&packet[kRootVector]) == kVectorRootExtended;
}

bool isSequenceAcceptable(std::uint8_t lastSequence, std::uint8_t receivedSequence) {
    // Signed 8-bit difference, as the standard specifies (handles wrap-around 255 -> 0).
    const int diff = static_cast<std::int8_t>(static_cast<std::uint8_t>(receivedSequence - lastSequence));
    return !(diff <= 0 && diff > -20);
}

Ipv4Address multicastAddress(std::uint16_t universe) {
    return Ipv4Address::fromOctets(239, 255, static_cast<std::uint8_t>(universe >> 8),
                                   static_cast<std::uint8_t>(universe & 0xFF));
}

Cid generateCid() {
    std::random_device device;
    std::mt19937_64 rng((static_cast<std::uint64_t>(device()) << 32) ^ device());
    Cid cid{};
    for (std::size_t i = 0; i < cid.size(); i += 8) {
        const std::uint64_t r = rng();
        for (std::size_t b = 0; b < 8; ++b) cid[i + b] = static_cast<std::uint8_t>(r >> (8 * b));
    }
    cid[6] = static_cast<std::uint8_t>((cid[6] & 0x0F) | 0x40);  // version 4 (random)
    cid[8] = static_cast<std::uint8_t>((cid[8] & 0x3F) | 0x80);  // RFC 4122 variant
    return cid;
}

std::string cidToString(const Cid& cid) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string text;
    text.reserve(36);
    for (std::size_t i = 0; i < cid.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) text.push_back('-');
        text.push_back(kHex[cid[i] >> 4]);
        text.push_back(kHex[cid[i] & 0x0F]);
    }
    return text;
}

std::optional<Cid> parseCid(std::string_view text) {
    auto hexValue = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    if (text.size() != 36) return std::nullopt;
    Cid cid{};
    std::size_t pos = 0;
    for (std::size_t i = 0; i < cid.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            if (text[pos] != '-') return std::nullopt;
            ++pos;
        }
        const int hi = hexValue(text[pos]);
        const int lo = hexValue(text[pos + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        cid[i] = static_cast<std::uint8_t>((hi << 4) | lo);
        pos += 2;
    }
    return cid;
}

}  // namespace dmxviz::dmx::sacn
