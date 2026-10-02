#pragma once
// Art-Net 4 packet encoding and decoding: pure functions, no sockets.
//
// Art-Net (Artistic Licence) carries DMX over UDP port 6454. A visualiser needs:
//   * ArtDmx       – DMX data, both directions.
//   * ArtPoll      – a console asks "who is there?"; we answer with
//   * ArtPollReply – which describes our ports so the console lists us and sends to us.
//   * ArtSync      – recognised and ignored: we show every frame as soon as it arrives.
//
// Universes are addressed by a 15-bit "port-address": Net (7 bits) : Sub-Net (4) : Universe (4).
// Reference: Art-Net 4 specification, Artistic Licence.

#include "dmx/NetAddress.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dmxviz::dmx::artnet {

constexpr std::uint16_t kDefaultPort = 6454;
constexpr std::uint16_t kProtocolVersion = 14;
constexpr std::uint16_t kMaxPortAddress = 0x7FFF;

constexpr std::size_t kDmxHeaderSize = 18;
constexpr std::size_t kMaxDmxPacketSize = kDmxHeaderSize + 512;
constexpr std::size_t kPollSize = 22;  // Art-Net 4 ArtPoll incl. targeted-mode fields
constexpr std::size_t kMinPollSize = 12;  // very old controllers send only ID + OpCode + version
constexpr std::size_t kPollReplySize = 239;
constexpr std::size_t kSyncSize = 14;

// The OpCodes we handle. Other valid Art-Net packets are recognised and ignored.
enum class OpCode : std::uint16_t {
    Poll = 0x2000,
    PollReply = 0x2100,
    Dmx = 0x5000,
    Sync = 0x5200,
};

// ArtPollReply "Style" codes (what kind of device a node is).
constexpr std::uint8_t kStyleNode = 0x00;
constexpr std::uint8_t kStyleController = 0x01;
constexpr std::uint8_t kStyleMedia = 0x02;
constexpr std::uint8_t kStyleRoute = 0x03;
constexpr std::uint8_t kStyleBackup = 0x04;
constexpr std::uint8_t kStyleConfig = 0x05;
constexpr std::uint8_t kStyleVisual = 0x06;  // visualiser: that's us

constexpr std::uint16_t kOemUnknown = 0x00FF;  // "OemUnknown" from the Art-Net OEM code list

// Port-address helpers.
constexpr std::uint8_t netOf(std::uint16_t portAddress) {
    return static_cast<std::uint8_t>((portAddress >> 8) & 0x7F);
}
constexpr std::uint8_t subNetOf(std::uint16_t portAddress) {
    return static_cast<std::uint8_t>((portAddress >> 4) & 0x0F);
}
constexpr std::uint8_t universeOf(std::uint16_t portAddress) { return static_cast<std::uint8_t>(portAddress & 0x0F); }
constexpr std::uint16_t makePortAddress(std::uint8_t net, std::uint8_t subNet, std::uint8_t universe) {
    return static_cast<std::uint16_t>(((net & 0x7F) << 8) | ((subNet & 0x0F) << 4) | (universe & 0x0F));
}

// Returns the OpCode of anything that starts with the "Art-Net\0" ID, else nullopt.
std::optional<std::uint16_t> opCodeOf(std::span<const std::uint8_t> packet);

// ---- ArtDmx ---------------------------------------------------------------------------

struct DmxPacket {
    std::uint8_t sequence = 0;            // 1..255 wrapping; 0 means "sequencing disabled"
    std::uint8_t physical = 0;            // sender's physical input port (informational)
    std::uint16_t portAddress = 0;        // 15-bit Net:SubNet:Universe
    std::span<const std::uint8_t> data;  // 1..512 channel values; after decode a view into the packet
};

// Writes an ArtDmx packet into `out` (needs up to kMaxDmxPacketSize bytes). The spec
// requires an even length of 2..512, so odd data is padded with a zero channel.
// Returns the packet size, or 0 if the input is invalid or `out` is too small.
std::size_t encodeDmx(const DmxPacket& packet, std::span<std::uint8_t> out);

// Validates ID, OpCode, protocol version (>= 14) and the length field. Odd lengths are
// accepted (some senders get this wrong) as long as the data is really there.
std::optional<DmxPacket> decodeDmx(std::span<const std::uint8_t> packet);

// ---- ArtPoll / ArtPollReply -----------------------------------------------------------

struct PollPacket {
    std::uint8_t flags = 0;
    std::uint8_t diagPriority = 0;
    // Art-Net 4 targeted mode (flags bit 5): only nodes with a port-address inside
    // [targetBottom, targetTop] should reply.
    std::uint16_t targetTop = kMaxPortAddress;
    std::uint16_t targetBottom = 0;

    bool targeted() const { return (flags & 0x20) != 0; }
};

std::size_t encodePoll(const PollPacket& poll, std::span<std::uint8_t> out);  // kPollSize bytes
std::optional<PollPacket> decodePoll(std::span<const std::uint8_t> packet);

// The fields of an ArtPollReply. Defaults describe a DmxViz visualiser; the interface
// fills in addresses, names and ports.
struct PollReply {
    Ipv4Address ip;
    std::uint16_t udpPort = kDefaultPort;
    std::uint16_t firmwareVersion = 1;
    std::uint8_t netSwitch = 0;     // port-address bits 14..8 shared by all ports in this reply
    std::uint8_t subSwitch = 0;     // port-address bits 7..4
    std::uint16_t oem = kOemUnknown;
    std::uint8_t status1 = 0xD0;    // indicators normal, port-address set locally
    std::uint16_t estaManufacturer = 0;
    std::string shortName;          // up to 17 characters
    std::string longName;           // up to 63 characters
    std::string nodeReport;         // up to 63 characters, "#xxxx [yyyy] text"
    std::uint8_t numPorts = 0;      // 0..4
    std::array<std::uint8_t, 4> portTypes{};    // bit 7: outputs DMX received from Art-Net
    std::array<std::uint8_t, 4> goodInput{};
    std::array<std::uint8_t, 4> goodOutputA{};
    std::array<std::uint8_t, 4> swIn{};         // low nibble of each input port-address
    std::array<std::uint8_t, 4> swOut{};        // low nibble of each output port-address
    std::uint8_t style = kStyleVisual;
    std::array<std::uint8_t, 6> mac{};
    Ipv4Address bindIp;
    std::uint8_t bindIndex = 1;     // 1-based page number when a node needs several replies
    std::uint8_t status2 = 0x08;    // bit 3: supports 15-bit port-addresses

    // Full output port-address of port `i` (0..3).
    std::uint16_t outputPortAddress(int i) const;
};

std::size_t encodePollReply(const PollReply& reply, std::span<std::uint8_t> out);  // kPollReplySize bytes
std::optional<PollReply> decodePollReply(std::span<const std::uint8_t> packet);

// An ArtPollReply describes at most four ports that share Net and Sub-Net, so a node
// with more (or more scattered) universes answers with several "pages". This splits
// `outputPortAddresses` accordingly; each page copies `base` and gets its own ports
// and bindIndex.
std::vector<PollReply> buildPollReplies(const PollReply& base, std::span<const std::uint16_t> outputPortAddresses);

// Formats an ArtPollReply NodeReport ("#0001 [0042] text"; 0x0001 is RcPowerOk) into
// `out`, reusing its capacity so answering a poll does not allocate.
void formatNodeReport(std::string& out, std::uint16_t statusCode, std::uint32_t counter, std::string_view text);

// ---- ArtSync --------------------------------------------------------------------------

std::size_t encodeSync(std::span<std::uint8_t> out);  // kSyncSize bytes
bool isSync(std::span<const std::uint8_t> packet);

}  // namespace dmxviz::dmx::artnet
