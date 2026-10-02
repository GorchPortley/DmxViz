#pragma once
// sACN (ANSI E1.31-2018, "Streaming ACN") data packet encoding and decoding: pure
// functions, no sockets.
//
// An E1.31 data packet is three nested PDUs (protocol data units), each starting with a
// 12-bit length plus 0x7 flags:
//   Root layer    – ACN identifier, vector "E1.31 data", sender CID (a UUID)
//   Framing layer – source name, priority, sync address, sequence, options, universe
//   DMP layer     – start code + up to 512 slots
// Sync and Discovery packets use a different root vector; we recognise and skip them.

#include "dmx/NetAddress.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace dmxviz::dmx::sacn {

constexpr std::uint16_t kDefaultPort = 5568;
constexpr std::uint16_t kMinUniverse = 1;
constexpr std::uint16_t kMaxUniverse = 63999;
constexpr std::uint8_t kDefaultPriority = 100;
constexpr std::uint8_t kMaxPriority = 200;

constexpr std::size_t kDataHeaderSize = 126;  // everything up to and including the start code
constexpr std::size_t kMaxDataPacketSize = kDataHeaderSize + 512;
constexpr std::size_t kSourceNameSize = 64;

// Root-layer vectors.
constexpr std::uint32_t kVectorRootData = 0x00000004;
constexpr std::uint32_t kVectorRootExtended = 0x00000008;  // sync + universe discovery

// Component identifier: a UUID that identifies a sender for its lifetime.
using Cid = std::array<std::uint8_t, 16>;

struct DataPacket {
    Cid cid{};
    std::string_view sourceName;          // after decode: a view into the packet
    std::uint8_t priority = kDefaultPriority;
    std::uint16_t syncAddress = 0;        // 0 = not synchronised
    std::uint8_t sequence = 0;
    bool preview = false;                 // data meant for visualisers (us!), not live output
    bool streamTerminated = false;        // sender is going away: drop it now
    bool forceSync = false;
    std::uint16_t universe = 1;           // 1..63999
    std::uint8_t startCode = 0;           // 0 = dimmer data; others (e.g. 0xDD) are not DMX levels
    std::span<const std::uint8_t> slots;  // up to 512 values after the start code
};

// Writes a complete data packet into `out` (needs kDataHeaderSize + slots bytes).
// Returns the packet size, or 0 on invalid input or a too small buffer.
std::size_t encodeData(const DataPacket& packet, std::span<std::uint8_t> out);

// Decodes and validates a data packet (all three layers, flags+length fields and
// vectors). Returns nullopt for anything malformed and for sync/discovery packets.
// Priorities above 200 are clamped to 200. Non-zero start codes are returned as-is:
// the caller decides to ignore them.
std::optional<DataPacket> decodeData(std::span<const std::uint8_t> packet);

// True for a well-formed root layer with the "extended" vector (sync or discovery).
bool isExtendedPacket(std::span<const std::uint8_t> packet);

// E1.31 section 6.7.2: a packet is out of order (and must be dropped) if, in signed
// 8-bit arithmetic, received - last is in (-20, 0]. Returns true if it should be used.
bool isSequenceAcceptable(std::uint8_t lastSequence, std::uint8_t receivedSequence);

// Multicast group for a universe: 239.255.<universe high byte>.<universe low byte>.
Ipv4Address multicastAddress(std::uint16_t universe);

// A random version-4 UUID.
Cid generateCid();
// "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" (lower case) and back.
std::string cidToString(const Cid& cid);
std::optional<Cid> parseCid(std::string_view text);

}  // namespace dmxviz::dmx::sacn
