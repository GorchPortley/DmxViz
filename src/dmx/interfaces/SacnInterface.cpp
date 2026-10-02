#include "dmx/interfaces/SacnInterface.h"

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

constexpr auto kReceiveTimeout = 50ms;      // how often the IO thread checks its stop flag
constexpr auto kForgetSequenceAfter = 10s;  // drop sequence state of senders gone this long
constexpr std::size_t kUniverseTableSize = sacn::kMaxUniverse + 1u;

bool validUniverse(int universe) {
    return universe >= sacn::kMinUniverse && universe <= sacn::kMaxUniverse;
}

}  // namespace

SacnInterface::SacnInterface()
    : subscribed_(kUniverseTableSize, false),
      sendSequence_(kUniverseTableSize, 0),
      sentUniverses_(kUniverseTableSize, false) {
    config_.cid = sacn::generateCid();
}

SacnInterface::~SacnInterface() {
    stop();
}

void SacnInterface::setConfig(SacnConfig newConfig) {
    // A config built from scratch has no CID; the sender identity must survive edits.
    if (newConfig.cid == sacn::Cid{}) newConfig.cid = config_.cid;
    config_ = std::move(newConfig);
}

bool SacnInterface::start(std::string& error) {
    if (running()) return true;
    stop();

    active_ = config_;
    destinationPort_ = active_.port != 0 ? active_.port : sacn::kDefaultPort;
    if (!active_.nic.isAny() && !active_.nic.isLoopback() && !findNetworkInterface(active_.nic)) {
        error = std::format("network interface {} not found", active_.nic.toString());
        setError(error);
        return false;
    }

    // Receive socket: wildcard address so multicast and unicast both arrive (loopback is
    // bound directly for tests and same-PC use).
    const Endpoint bindTo{active_.nic.isLoopback() ? active_.nic : kAnyAddress, active_.port};
    if (!receiveSocket_.open(bindTo, true, error)) {
        setError(error);
        return false;
    }
    localPort_ = receiveSocket_.localEndpoint().port;

    // Send socket: ephemeral port on the chosen NIC.
    if (!sendSocket_.open({active_.nic, 0}, false, error)) {
        receiveSocket_.close();
        setError(error);
        return false;
    }
    std::string warning;
    if (!active_.nic.isAny() && !sendSocket_.setMulticastInterface(active_.nic, warning))
        log::warn("dmx", "sACN: {}", warning);

    std::fill(subscribed_.begin(), subscribed_.end(), false);
    int joinFailures = 0;
    for (std::uint16_t universe : active_.universes) {
        if (!validUniverse(universe)) continue;
        subscribed_[universe] = true;
        if (active_.multicastInput &&
            !receiveSocket_.joinMulticast(sacn::multicastAddress(universe), active_.nic, warning)) {
            if (joinFailures++ == 0) log::warn("dmx", "sACN: {}", warning);
        }
    }

    sequences_.clear();
    std::fill(sendSequence_.begin(), sendSequence_.end(), std::uint8_t{0});
    std::fill(sentUniverses_.begin(), sentUniverses_.end(), false);

    stopRequested_ = false;
    try {
        thread_ = std::thread(&SacnInterface::run, this);
    } catch (const std::system_error& e) {
        error = std::format("cannot start IO thread: {}", e.what());
        receiveSocket_.close();
        sendSocket_.close();
        setError(error);
        return false;
    }

    std::string message = std::format("listening on {}:{}", bindTo.address.toString(), localPort_.load());
    if (joinFailures > 0)  // unicast still works, so this is not fatal
        message += std::format("; multicast join failed for {} universe(s)", joinFailures);
    setRunning(std::move(message));
    return true;
}

void SacnInterface::stop() {
    stopRequested_ = true;
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard lock(sendMutex_);
        if (sendSocket_.isOpen()) sendTerminations();
        sendSocket_.close();
        receiveSocket_.close();  // also leaves the multicast groups
    }
    localPort_ = 0;
    releaseInputs();
    if (state() != InterfaceState::Stopped) setStopped();
}

void SacnInterface::run() {
    while (!stopRequested_.load()) {
        Endpoint from;
        const int size = receiveSocket_.receive(receiveBuffer_, from, kReceiveTimeout);
        if (size < 0) {
            countInvalid();
            std::this_thread::sleep_for(20ms);  // do not spin on a broken socket
            continue;
        }
        if (size > 0) handlePacket(std::span(receiveBuffer_).first(static_cast<std::size_t>(size)), from);
    }
}

void SacnInterface::handlePacket(std::span<const std::uint8_t> bytes, const Endpoint& from) {
    const auto packet = sacn::decodeData(bytes);
    if (!packet) {
        // Sync and discovery packets are valid E1.31 we do not need.
        if (sacn::isExtendedPacket(bytes))
            countIn();
        else
            countInvalid();
        return;
    }
    if (packet->cid == active_.cid) return;  // our own multicast, looped back by the OS
    if (!active_.acceptAllUniverses && !subscribed_[packet->universe]) return;  // someone else's universe
    if (packet->startCode != 0 || (packet->preview && !active_.acceptPreview)) {
        countIn();  // valid, but not dimmer levels for us (e.g. 0xDD per-address priority)
        return;
    }
    const int universe = int{packet->universe} + active_.universeOffset;
    if (universe < 1 || universe > 0xFFFF) return;

    const SourceId sourceId = SourceId::fromCid(id(), packet->cid);
    if (packet->streamTerminated) {
        forgetSequence(packet->cid, packet->universe);
        removeInput(static_cast<UniverseId>(universe), sourceId);
        return;
    }
    if (!acceptSequence(packet->cid, packet->universe, packet->sequence, Clock::now())) {
        countInvalid();  // late or duplicate
        return;
    }

    SourceDescriptor source;
    source.id = sourceId;
    source.protocol = Protocol::Sacn;
    source.priority = packet->priority;
    source.endpoint = from;
    source.name = packet->sourceName;
    source.preview = packet->preview;
    submitInput(static_cast<UniverseId>(universe), source, packet->slots);
}

bool SacnInterface::acceptSequence(const sacn::Cid& cid, std::uint16_t universe, std::uint8_t sequence, TimePoint now) {
    for (SequenceState& entry : sequences_) {
        if (entry.universe != universe || entry.cid != cid) continue;
        // After a data-loss timeout the sender may have restarted with any number.
        const bool restarted = now - entry.lastSeen > kDefaultSourceTimeout;
        if (!restarted && !sacn::isSequenceAcceptable(entry.last, sequence)) return false;
        entry.last = sequence;
        entry.lastSeen = now;
        return true;
    }
    // A new sender: tidy up first so the table cannot grow forever.
    std::erase_if(sequences_, [&](const SequenceState& s) { return now - s.lastSeen > kForgetSequenceAfter; });
    sequences_.push_back({cid, universe, sequence, now});
    return true;
}

void SacnInterface::forgetSequence(const sacn::Cid& cid, std::uint16_t universe) {
    std::erase_if(sequences_, [&](const SequenceState& s) { return s.universe == universe && s.cid == cid; });
}

void SacnInterface::send(UniverseId universe, const UniverseData& data) {
    std::lock_guard lock(sendMutex_);
    if (!running() || !sendSocket_.isOpen()) return;
    const int sacnUniverse = int{universe} - active_.universeOffset;
    if (!validUniverse(sacnUniverse)) {
        countInvalid();  // no sACN universe corresponds with the current offset
        return;
    }
    sendPacket(static_cast<std::uint16_t>(sacnUniverse), data, false);
    sentUniverses_[static_cast<std::size_t>(sacnUniverse)] = true;
}

void SacnInterface::sendPacket(std::uint16_t universe, std::span<const std::uint8_t> slots, bool terminated) {
    sacn::DataPacket packet;
    packet.cid = active_.cid;
    packet.sourceName = active_.sourceName;
    packet.priority = active_.priority;
    packet.sequence = ++sendSequence_[universe];  // wraps 255 -> 0, as E1.31 expects
    packet.streamTerminated = terminated;
    packet.universe = universe;
    packet.slots = slots;
    const std::size_t size = sacn::encodeData(packet, sendBuffer_);
    const std::span<const std::uint8_t> bytes(sendBuffer_.data(), size);

    if (active_.multicastOutput) {
        if (sendSocket_.sendTo(bytes, {sacn::multicastAddress(universe), destinationPort_})) countOut();
    }
    for (const Endpoint& target : active_.unicastTargets)
        if (sendSocket_.sendTo(bytes, target)) countOut();
}

void SacnInterface::sendTerminations() {
    // E1.31 6.2.6: a source that stops sends three packets with the terminated flag.
    static const UniverseData kZeros{};
    for (std::size_t universe = 0; universe < sentUniverses_.size(); ++universe) {
        if (!sentUniverses_[universe]) continue;
        for (int i = 0; i < 3; ++i) sendPacket(static_cast<std::uint16_t>(universe), kZeros, true);
        sentUniverses_[universe] = false;
    }
}

nlohmann::json SacnInterface::saveConfig() const {
    return {
        {"nic", config_.nic.toString()},
        {"port", config_.port},
        {"universeOffset", config_.universeOffset},
        {"universes", config_.universes},
        {"multicastInput", config_.multicastInput},
        {"acceptAllUniverses", config_.acceptAllUniverses},
        {"acceptPreview", config_.acceptPreview},
        {"sourceName", config_.sourceName},
        {"priority", config_.priority},
        {"output", config_.multicastOutput ? "multicast" : "unicast"},
        {"unicastTargets", config::endpointsToJson(config_.unicastTargets)},
        {"cid", sacn::cidToString(config_.cid)},
    };
}

bool SacnInterface::loadConfig(const nlohmann::json& settings, std::string& error) {
    try {
        SacnConfig c;
        c.cid = config_.cid;  // keep ours unless the file has one
        if (!config::readAddress(settings, "nic", c.nic, error)) return false;
        const int port = settings.value("port", int{c.port});
        if (port < 0 || port > 0xFFFF) {
            error = std::format("invalid sACN port {}", port);
            return false;
        }
        c.port = static_cast<std::uint16_t>(port);
        c.universeOffset = settings.value("universeOffset", c.universeOffset);
        if (!config::readUniverses(settings, "universes", sacn::kMinUniverse, sacn::kMaxUniverse, c.universes, error))
            return false;
        c.multicastInput = settings.value("multicastInput", c.multicastInput);
        c.acceptAllUniverses = settings.value("acceptAllUniverses", c.acceptAllUniverses);
        c.acceptPreview = settings.value("acceptPreview", c.acceptPreview);
        c.sourceName = settings.value("sourceName", c.sourceName);
        c.priority = static_cast<std::uint8_t>(
            std::clamp(settings.value("priority", int{c.priority}), 0, int{sacn::kMaxPriority}));
        const std::string output = settings.value("output", std::string("multicast"));
        if (output != "multicast" && output != "unicast") {
            error = std::format("unknown sACN output mode '{}'", output);
            return false;
        }
        c.multicastOutput = output == "multicast";
        const std::uint16_t destinationPort = c.port != 0 ? c.port : sacn::kDefaultPort;
        if (!config::readEndpoints(settings, "unicastTargets", destinationPort, c.unicastTargets, error)) return false;
        if (settings.contains("cid")) {
            const auto cid = sacn::parseCid(settings.at("cid").get<std::string>());
            if (!cid) {
                error = "'cid' is not a UUID";
                return false;
            }
            c.cid = *cid;
        }
        config_ = std::move(c);
        return true;
    } catch (const nlohmann::json::exception& e) {
        error = std::format("sACN settings: {}", e.what());
        return false;
    }
}

std::string SacnInterface::summary() const {
    return std::format("sACN on {}:{}", config_.nic.isAny() ? std::string("all interfaces") : config_.nic.toString(),
                       config_.port);
}

}  // namespace dmxviz::dmx
