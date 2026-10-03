#include "dmx/protocol/ArtNet.h"

#include "dmx/protocol/ByteIo.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <iterator>

namespace dmxviz::dmx::artnet {
namespace {

constexpr std::array<std::uint8_t, 8> kId = {'A', 'r', 't', '-', 'N', 'e', 't', 0};

// Byte offsets inside an ArtPollReply (Art-Net 4, table "ArtPollReply packet definition").
namespace reply {
constexpr std::size_t kIp = 10;
constexpr std::size_t kPort = 14;
constexpr std::size_t kVersInfo = 16;
constexpr std::size_t kNetSwitch = 18;
constexpr std::size_t kSubSwitch = 19;
constexpr std::size_t kOem = 20;
constexpr std::size_t kUbeaVersion = 22;
constexpr std::size_t kStatus1 = 23;
constexpr std::size_t kEstaMan = 24;  // low byte first!
constexpr std::size_t kShortName = 26;
constexpr std::size_t kShortNameSize = 18;
constexpr std::size_t kLongName = 44;
constexpr std::size_t kLongNameSize = 64;
constexpr std::size_t kNodeReport = 108;
constexpr std::size_t kNodeReportSize = 64;
constexpr std::size_t kNumPorts = 172;
constexpr std::size_t kPortTypes = 174;
constexpr std::size_t kGoodInput = 178;
constexpr std::size_t kGoodOutputA = 182;
constexpr std::size_t kSwIn = 186;
constexpr std::size_t kSwOut = 190;
constexpr std::size_t kStyle = 200;
constexpr std::size_t kMac = 201;
constexpr std::size_t kBindIp = 207;
constexpr std::size_t kBindIndex = 211;
constexpr std::size_t kStatus2 = 212;
constexpr std::size_t kMinDecodeSize = kBindIp;  // older nodes may stop after the MAC
}  // namespace reply

bool hasId(std::span<const std::uint8_t> packet) {
    return packet.size() >= 10 && std::equal(kId.begin(), kId.end(), packet.begin());
}

// ID + OpCode + ProtVer: the common header of every packet except ArtPollReply.
bool hasHeader(std::span<const std::uint8_t> packet, OpCode op, std::size_t minSize) {
    if (packet.size() < minSize || packet.size() < 12) return false;
    if (opCodeOf(packet) != static_cast<std::uint16_t>(op)) return false;
    return bytes::readU16BE(&packet[10]) >= kProtocolVersion;
}

void writeHeader(std::span<std::uint8_t> out, OpCode op) {
    std::copy(kId.begin(), kId.end(), out.begin());
    bytes::writeU16LE(&out[8], static_cast<std::uint16_t>(op));
}

void writeVersion(std::span<std::uint8_t> out) {
    bytes::writeU16BE(&out[10], kProtocolVersion);
}

// Copies text into a fixed, NUL-terminated field (truncating so the NUL always fits).
void writeText(std::uint8_t* field, std::size_t fieldSize, std::string_view text) {
    std::memset(field, 0, fieldSize);
    const std::size_t n = std::min(text.size(), fieldSize - 1);
    std::copy_n(text.begin(), n, field);  // not memcpy: an empty view may hold a null pointer
}

std::string readText(const std::uint8_t* field, std::size_t fieldSize) {
    std::size_t n = 0;
    while (n < fieldSize && field[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(field), n);
}

}  // namespace

std::optional<std::uint16_t> opCodeOf(std::span<const std::uint8_t> packet) {
    if (!hasId(packet)) return std::nullopt;
    return bytes::readU16LE(&packet[8]);
}

// ---- ArtDmx -----------------------------------------------------------------------------

std::size_t encodeDmx(const DmxPacket& packet, std::span<std::uint8_t> out) {
    if (packet.data.empty() || packet.data.size() > 512 || packet.portAddress > kMaxPortAddress) return 0;
    const std::size_t length = std::max<std::size_t>(2, (packet.data.size() + 1) & ~std::size_t{1});
    const std::size_t total = kDmxHeaderSize + length;
    if (out.size() < total) return 0;

    writeHeader(out, OpCode::Dmx);
    writeVersion(out);
    out[12] = packet.sequence;
    out[13] = packet.physical;
    out[14] = static_cast<std::uint8_t>(packet.portAddress & 0xFF);  // SubUni
    out[15] = netOf(packet.portAddress);                             // Net
    bytes::writeU16BE(&out[16], static_cast<std::uint16_t>(length));
    std::copy(packet.data.begin(), packet.data.end(), out.begin() + kDmxHeaderSize);
    std::fill(out.begin() + static_cast<std::ptrdiff_t>(kDmxHeaderSize + packet.data.size()),
              out.begin() + static_cast<std::ptrdiff_t>(total), std::uint8_t{0});
    return total;
}

std::optional<DmxPacket> decodeDmx(std::span<const std::uint8_t> packet) {
    if (!hasHeader(packet, OpCode::Dmx, kDmxHeaderSize)) return std::nullopt;
    const std::size_t length = bytes::readU16BE(&packet[16]);
    if (length < 1 || length > 512 || packet.size() < kDmxHeaderSize + length) return std::nullopt;

    DmxPacket result;
    result.sequence = packet[12];
    result.physical = packet[13];
    result.portAddress = static_cast<std::uint16_t>(((packet[15] & 0x7F) << 8) | packet[14]);
    result.data = packet.subspan(kDmxHeaderSize, length);
    return result;
}

// ---- ArtPoll ----------------------------------------------------------------------------

std::size_t encodePoll(const PollPacket& poll, std::span<std::uint8_t> out) {
    if (out.size() < kPollSize) return 0;
    std::fill(out.begin(), out.begin() + kPollSize, std::uint8_t{0});
    writeHeader(out, OpCode::Poll);
    writeVersion(out);
    out[12] = poll.flags;
    out[13] = poll.diagPriority;
    bytes::writeU16BE(&out[14], poll.targetTop);
    bytes::writeU16BE(&out[16], poll.targetBottom);
    return kPollSize;  // EstaMan and Oem (18..21) left zero
}

std::optional<PollPacket> decodePoll(std::span<const std::uint8_t> packet) {
    if (!hasHeader(packet, OpCode::Poll, kMinPollSize)) return std::nullopt;
    PollPacket poll;
    if (packet.size() >= 14) {
        poll.flags = packet[12];
        poll.diagPriority = packet[13];
    }
    if (packet.size() >= 18) {
        poll.targetTop = bytes::readU16BE(&packet[14]);
        poll.targetBottom = bytes::readU16BE(&packet[16]);
    } else {
        poll.flags = static_cast<std::uint8_t>(poll.flags & ~0x20);  // no target fields: cannot be targeted
    }
    return poll;
}

// ---- ArtPollReply -----------------------------------------------------------------------

std::uint16_t PollReply::outputPortAddress(int i) const {
    return makePortAddress(netSwitch, subSwitch, swOut[static_cast<std::size_t>(i)]);
}

std::size_t encodePollReply(const PollReply& r, std::span<std::uint8_t> out) {
    if (out.size() < kPollReplySize) return 0;
    std::fill(out.begin(), out.begin() + kPollReplySize, std::uint8_t{0});
    writeHeader(out, OpCode::PollReply);  // note: ArtPollReply has no ProtVer field

    bytes::writeU32BE(&out[reply::kIp], r.ip.value);
    bytes::writeU16LE(&out[reply::kPort], r.udpPort);  // "transmitted low byte first"
    bytes::writeU16BE(&out[reply::kVersInfo], r.firmwareVersion);
    out[reply::kNetSwitch] = static_cast<std::uint8_t>(r.netSwitch & 0x7F);
    out[reply::kSubSwitch] = static_cast<std::uint8_t>(r.subSwitch & 0x0F);
    bytes::writeU16BE(&out[reply::kOem], r.oem);
    out[reply::kUbeaVersion] = 0;
    out[reply::kStatus1] = r.status1;
    bytes::writeU16LE(&out[reply::kEstaMan], r.estaManufacturer);
    writeText(&out[reply::kShortName], reply::kShortNameSize, r.shortName);
    writeText(&out[reply::kLongName], reply::kLongNameSize, r.longName);
    writeText(&out[reply::kNodeReport], reply::kNodeReportSize, r.nodeReport);
    bytes::writeU16BE(&out[reply::kNumPorts], std::min<std::uint8_t>(r.numPorts, 4));
    for (std::size_t i = 0; i < 4; ++i) {
        out[reply::kPortTypes + i] = r.portTypes[i];
        out[reply::kGoodInput + i] = r.goodInput[i];
        out[reply::kGoodOutputA + i] = r.goodOutputA[i];
        out[reply::kSwIn + i] = static_cast<std::uint8_t>(r.swIn[i] & 0x0F);
        out[reply::kSwOut + i] = static_cast<std::uint8_t>(r.swOut[i] & 0x0F);
    }
    out[reply::kStyle] = r.style;
    std::copy(r.mac.begin(), r.mac.end(), out.begin() + reply::kMac);
    bytes::writeU32BE(&out[reply::kBindIp], r.bindIp.value);
    out[reply::kBindIndex] = r.bindIndex;
    out[reply::kStatus2] = r.status2;
    return kPollReplySize;
}

std::optional<PollReply> decodePollReply(std::span<const std::uint8_t> packet) {
    if (packet.size() < reply::kMinDecodeSize) return std::nullopt;
    if (opCodeOf(packet) != static_cast<std::uint16_t>(OpCode::PollReply)) return std::nullopt;

    PollReply r;
    r.ip = Ipv4Address{bytes::readU32BE(&packet[reply::kIp])};
    r.udpPort = bytes::readU16LE(&packet[reply::kPort]);
    r.firmwareVersion = bytes::readU16BE(&packet[reply::kVersInfo]);
    r.netSwitch = static_cast<std::uint8_t>(packet[reply::kNetSwitch] & 0x7F);
    r.subSwitch = static_cast<std::uint8_t>(packet[reply::kSubSwitch] & 0x0F);
    r.oem = bytes::readU16BE(&packet[reply::kOem]);
    r.status1 = packet[reply::kStatus1];
    r.estaManufacturer = bytes::readU16LE(&packet[reply::kEstaMan]);
    r.shortName = readText(&packet[reply::kShortName], reply::kShortNameSize);
    r.longName = readText(&packet[reply::kLongName], reply::kLongNameSize);
    r.nodeReport = readText(&packet[reply::kNodeReport], reply::kNodeReportSize);
    r.numPorts = static_cast<std::uint8_t>(std::min<std::uint16_t>(bytes::readU16BE(&packet[reply::kNumPorts]), 4));
    for (std::size_t i = 0; i < 4; ++i) {
        r.portTypes[i] = packet[reply::kPortTypes + i];
        r.goodInput[i] = packet[reply::kGoodInput + i];
        r.goodOutputA[i] = packet[reply::kGoodOutputA + i];
        r.swIn[i] = static_cast<std::uint8_t>(packet[reply::kSwIn + i] & 0x0F);
        r.swOut[i] = static_cast<std::uint8_t>(packet[reply::kSwOut + i] & 0x0F);
    }
    r.style = packet[reply::kStyle];
    std::copy_n(packet.begin() + reply::kMac, 6, r.mac.begin());
    if (packet.size() >= reply::kStatus2 + 1) {
        r.bindIp = Ipv4Address{bytes::readU32BE(&packet[reply::kBindIp])};
        r.bindIndex = packet[reply::kBindIndex];
        r.status2 = packet[reply::kStatus2];
    }
    return r;
}

std::vector<PollReply> buildPollReplies(const PollReply& base, std::span<const std::uint16_t> outputPortAddresses) {
    std::vector<std::uint16_t> sorted(outputPortAddresses.begin(), outputPortAddresses.end());
    for (auto& pa : sorted) pa = static_cast<std::uint16_t>(pa & kMaxPortAddress);
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    std::vector<PollReply> pages;
    for (std::size_t i = 0; i < sorted.size();) {
        PollReply page = base;
        page.netSwitch = netOf(sorted[i]);
        page.subSwitch = subNetOf(sorted[i]);
        page.numPorts = 0;
        page.portTypes = {};
        page.goodInput = {};
        page.goodOutputA = {};
        page.swOut = {};
        // Fill up to four ports while Net and Sub-Net stay the same.
        while (i < sorted.size() && page.numPorts < 4 && netOf(sorted[i]) == page.netSwitch &&
               subNetOf(sorted[i]) == page.subSwitch) {
            const std::size_t port = page.numPorts++;
            page.portTypes[port] = 0x80;  // can output DMX512 received from Art-Net: consoles send to us
            page.goodInput[port] = 0x08;  // input disabled (we never transmit on these ports)
            page.swOut[port] = universeOf(sorted[i]);
            ++i;
        }
        page.bindIndex = static_cast<std::uint8_t>(std::min<std::size_t>(pages.size() + 1, 255));
        pages.push_back(std::move(page));
    }
    return pages;
}

void formatNodeReport(std::string& out, std::uint16_t statusCode, std::uint32_t counter, std::string_view text) {
    out.clear();
    std::format_to(std::back_inserter(out), "#{:04x} [{:04}] {}", statusCode, counter % 10000, text);
    if (out.size() > reply::kNodeReportSize - 1) out.resize(reply::kNodeReportSize - 1);
}

// ---- ArtSync ----------------------------------------------------------------------------

std::size_t encodeSync(std::span<std::uint8_t> out) {
    if (out.size() < kSyncSize) return 0;
    std::fill(out.begin(), out.begin() + kSyncSize, std::uint8_t{0});
    writeHeader(out, OpCode::Sync);
    writeVersion(out);
    return kSyncSize;  // Aux1, Aux2 = 0
}

bool isSync(std::span<const std::uint8_t> packet) {
    return hasHeader(packet, OpCode::Sync, kSyncSize);
}

}  // namespace dmxviz::dmx::artnet
