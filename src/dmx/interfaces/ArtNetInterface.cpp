#include "dmx/interfaces/ArtNetInterface.h"

#include "core/Log.h"
#include "dmx/interfaces/ConfigJson.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <format>
#include <system_error>

namespace dmxviz::dmx {
namespace {

using namespace std::chrono_literals;

constexpr auto kReceiveTimeout = 50ms;  // how often the IO thread checks its stop flag
constexpr auto kEchoWindow = 3s;        // our own broadcasts may come back this long after sending
constexpr std::string_view kSourceName = "Art-Net";

}  // namespace

ArtNetInterface::ArtNetInterface() : sequence_(artnet::kMaxPortAddress + 1u, 0) {}

ArtNetInterface::~ArtNetInterface() {
    stop();
}

bool ArtNetInterface::start(std::string& error) {
    if (running()) return true;
    stop();  // clean up after a previous error

    active_ = config_;
    localInterfaces_ = listNetworkInterfaces();
    nic_.reset();
    if (!active_.nic.isAny()) {
        for (const NetworkInterfaceInfo& info : localInterfaces_)
            if (info.address == active_.nic) nic_ = info;
        if (!nic_ && !active_.nic.isLoopback()) {
            error = std::format("network interface {} not found", active_.nic.toString());
            setError(error);
            return false;
        }
    }

    // See the header: only loopback addresses are bound directly.
    const Endpoint bindTo{active_.nic.isLoopback() ? active_.nic : kAnyAddress, active_.port};
    if (!socket_.open(bindTo, true, error) || !socket_.setBroadcast(true, error)) {
        socket_.close();
        setError(error);
        return false;
    }
    localPort_ = socket_.localEndpoint().port;

    const std::uint16_t destinationPort = active_.port != 0 ? active_.port : artnet::kDefaultPort;
    broadcastTarget_ = Endpoint{nic_ ? nic_->broadcast : kLimitedBroadcast, destinationPort};
    std::fill(sequence_.begin(), sequence_.end(), std::uint8_t{0});
    lastSendTicks_ = 0;
    buildPollReplies();

    stopRequested_ = false;
    try {
        thread_ = std::thread(&ArtNetInterface::run, this);
    } catch (const std::system_error& e) {
        error = std::format("cannot start IO thread: {}", e.what());
        socket_.close();
        setError(error);
        return false;
    }
    setRunning(std::format("listening on {}:{}", bindTo.address.toString(), localPort_.load()));
    return true;
}

void ArtNetInterface::stop() {
    stopRequested_ = true;
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard lock(sendMutex_);
        socket_.close();
    }
    localPort_ = 0;
    releaseInputs();
    if (state() != InterfaceState::Stopped) setStopped();
}

void ArtNetInterface::run() {
    while (!stopRequested_.load()) {
        Endpoint from;
        const int size = socket_.receive(receiveBuffer_, from, kReceiveTimeout);
        if (size < 0) {
            countInvalid();
            std::this_thread::sleep_for(20ms);  // do not spin on a broken socket
            continue;
        }
        if (size > 0) handlePacket(std::span(receiveBuffer_).first(static_cast<std::size_t>(size)), from);
    }
}

void ArtNetInterface::handlePacket(std::span<const std::uint8_t> packet, const Endpoint& from) {
    const auto op = artnet::opCodeOf(packet);
    if (!op) {
        countInvalid();  // not Art-Net at all
        return;
    }
    switch (static_cast<artnet::OpCode>(*op)) {
        case artnet::OpCode::Dmx:
            handleDmx(packet, from);
            break;
        case artnet::OpCode::Poll:
            handlePoll(packet, from);
            break;
        case artnet::OpCode::Sync:
            // Tolerated: we show each ArtDmx frame as soon as it arrives.
            if (artnet::isSync(packet))
                countIn();
            else
                countInvalid();
            break;
        default:
            break;  // ArtPollReply from other nodes, RDM, ...: not our business
    }
}

void ArtNetInterface::handleDmx(std::span<const std::uint8_t> packet, const Endpoint& from) {
    const auto dmx = artnet::decodeDmx(packet);
    if (!dmx) {
        countInvalid();
        return;
    }
    if (!acceptSender(from) || isOwnEcho(from)) return;
    const int universe = int{dmx->portAddress} + 1 + active_.universeOffset;
    if (universe < 1 || universe > 0xFFFF) return;  // outside the logical range after the offset

    SourceDescriptor source;
    source.id = SourceId::fromEndpoint(id(), from);
    source.protocol = Protocol::ArtNet;
    source.endpoint = from;
    source.name = kSourceName;
    submitInput(static_cast<UniverseId>(universe), source, dmx->data);
}

void ArtNetInterface::handlePoll(std::span<const std::uint8_t> packet, const Endpoint& from) {
    const auto poll = artnet::decodePoll(packet);
    if (!poll) {
        countInvalid();
        return;
    }
    countIn();
    if (!active_.replyToPoll || !acceptSender(from)) return;

    if (poll->targeted()) {
        // Art-Net 4 targeted mode: answer only if one of our ports is in the requested range.
        bool inRange = false;
        for (const artnet::PollReply& page : replyPages_)
            for (int i = 0; i < page.numPorts; ++i) {
                const std::uint16_t pa = page.outputPortAddress(i);
                inRange = inRange || (pa >= poll->targetBottom && pa <= poll->targetTop);
            }
        if (!inRange) return;
    }

    // Reply with the address of the NIC that faces the poller, unicast to the poller.
    const NetworkInterfaceInfo* nic = replyInterfaceFor(from.address);
    ++pollReplyCount_;
    for (artnet::PollReply& page : replyPages_) {
        page.ip = nic ? nic->address : active_.nic;
        page.bindIp = page.ip;
        page.mac = nic ? nic->mac : std::array<std::uint8_t, 6>{};
        artnet::formatNodeReport(page.nodeReport, 0x0001, pollReplyCount_, "DmxViz running");
        const std::size_t size = artnet::encodePollReply(page, replyBuffer_);
        std::lock_guard lock(sendMutex_);
        if (socket_.sendTo(std::span<const std::uint8_t>(replyBuffer_.data(), size), from)) countOut();
    }
}

void ArtNetInterface::send(UniverseId universe, const UniverseData& data) {
    std::lock_guard lock(sendMutex_);
    if (!running() || !socket_.isOpen()) return;
    const int portAddress = int{universe} - 1 - active_.universeOffset;
    if (portAddress < 0 || portAddress > artnet::kMaxPortAddress) {
        countInvalid();  // this logical universe has no Art-Net equivalent with the current offset
        return;
    }

    // Sequence numbers run 1..255 and wrap; 0 would mean "no sequencing".
    std::uint8_t& sequence = sequence_[static_cast<std::size_t>(portAddress)];
    sequence = static_cast<std::uint8_t>(sequence == 255 ? 1 : sequence + 1);
    artnet::DmxPacket packet;
    packet.sequence = sequence;
    packet.portAddress = static_cast<std::uint16_t>(portAddress);
    packet.data = data;
    const std::size_t size = artnet::encodeDmx(packet, sendBuffer_);
    const std::span<const std::uint8_t> bytes(sendBuffer_.data(), size);

    lastSendTicks_ = Clock::now().time_since_epoch().count();
    if (active_.broadcastOutput) {
        if (socket_.sendTo(bytes, broadcastTarget_)) countOut();
    }
    for (const Endpoint& target : active_.unicastTargets)
        if (socket_.sendTo(bytes, target)) countOut();
}

bool ArtNetInterface::isLocalAddress(Ipv4Address address) const {
    if (address == active_.nic) return true;
    return std::any_of(localInterfaces_.begin(), localInterfaces_.end(),
                       [&](const NetworkInterfaceInfo& info) { return info.address == address; });
}

bool ArtNetInterface::acceptSender(const Endpoint& from) const {
    // With a specific (non-loopback) NIC we listen on all interfaces; keep its subnet only.
    if (!nic_ || active_.nic.isLoopback()) return true;
    return nic_->sameSubnet(from.address) || isLocalAddress(from.address);
}

bool ArtNetInterface::isOwnEcho(const Endpoint& from) const {
    // Broadcasts we send come back to our own socket. Only filter while we are actually
    // sending, so a console on the same PC still works when our output is idle.
    const Clock::rep last = lastSendTicks_.load();
    if (last == 0 || Clock::now() - TimePoint(Clock::duration(last)) > kEchoWindow) return false;
    return from.port == localPort_.load() && isLocalAddress(from.address);
}

const NetworkInterfaceInfo* ArtNetInterface::replyInterfaceFor(Ipv4Address poller) const {
    if (nic_) return &*nic_;
    for (const NetworkInterfaceInfo& info : localInterfaces_)
        if (info.up && !info.loopback && info.sameSubnet(poller)) return &info;
    for (const NetworkInterfaceInfo& info : localInterfaces_)
        if (info.loopback == poller.isLoopback() && info.up) return &info;
    return nullptr;
}

void ArtNetInterface::buildPollReplies() {
    std::vector<std::uint16_t> portAddresses;
    for (UniverseId universe : active_.announcedUniverses) {
        const int pa = int{universe} - 1 - active_.universeOffset;
        if (pa >= 0 && pa <= artnet::kMaxPortAddress) portAddresses.push_back(static_cast<std::uint16_t>(pa));
    }
    artnet::PollReply base;
    base.udpPort = artnet::kDefaultPort;
    base.shortName = active_.shortName;
    base.longName = active_.longName;
    base.nodeReport.reserve(64);  // formatNodeReport() then never allocates
    replyPages_ = artnet::buildPollReplies(base, portAddresses);
    if (replyPages_.empty()) replyPages_.push_back(base);  // still announce ourselves, with no ports
    pollReplyCount_ = 0;
}

nlohmann::json ArtNetInterface::saveConfig() const {
    nlohmann::json universes = nlohmann::json::array();
    for (UniverseId u : config_.announcedUniverses) universes.push_back(u);
    return {
        {"nic", config_.nic.toString()},
        {"port", config_.port},
        {"universeOffset", config_.universeOffset},
        {"output", config_.broadcastOutput ? "broadcast" : "unicast"},
        {"unicastTargets", config::endpointsToJson(config_.unicastTargets)},
        {"replyToPoll", config_.replyToPoll},
        {"announcedUniverses", universes},
        {"shortName", config_.shortName},
        {"longName", config_.longName},
    };
}

bool ArtNetInterface::loadConfig(const nlohmann::json& settings, std::string& error) {
    try {
        ArtNetConfig c;
        if (!config::readAddress(settings, "nic", c.nic, error)) return false;
        const int port = settings.value("port", int{c.port});
        if (port < 0 || port > 0xFFFF) {
            error = std::format("invalid Art-Net port {}", port);
            return false;
        }
        c.port = static_cast<std::uint16_t>(port);
        c.universeOffset = settings.value("universeOffset", c.universeOffset);
        const std::string output = settings.value("output", std::string("broadcast"));
        if (output != "broadcast" && output != "unicast") {
            error = std::format("unknown Art-Net output mode '{}'", output);
            return false;
        }
        c.broadcastOutput = output == "broadcast";
        const std::uint16_t destinationPort = c.port != 0 ? c.port : artnet::kDefaultPort;
        if (!config::readEndpoints(settings, "unicastTargets", destinationPort, c.unicastTargets, error)) return false;
        c.replyToPoll = settings.value("replyToPoll", c.replyToPoll);
        if (!config::readUniverses(settings, "announcedUniverses", 1, 0xFFFF, c.announcedUniverses, error))
            return false;
        c.shortName = settings.value("shortName", c.shortName);
        c.longName = settings.value("longName", c.longName);
        config_ = std::move(c);
        return true;
    } catch (const nlohmann::json::exception& e) {
        error = std::format("Art-Net settings: {}", e.what());
        return false;
    }
}

std::string ArtNetInterface::summary() const {
    return std::format("Art-Net on {}:{}", config_.nic.isAny() ? std::string("all interfaces") : config_.nic.toString(),
                       config_.port);
}

}  // namespace dmxviz::dmx
