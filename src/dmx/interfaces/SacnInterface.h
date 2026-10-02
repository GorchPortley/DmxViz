#pragma once
// SacnInterface: sACN (E1.31) input and output on one network interface.
//
// Input: listens on UDP 5568 (configurable) and joins the multicast group of every
// subscribed universe on the chosen NIC; unicast packets arrive on the same socket.
// Each sender (CID) of each universe becomes a source in the store with its own
// priority. Out-of-order packets are dropped (E1.31 6.7.2) and a stream-terminated
// packet removes its source at once. Preview data is accepted by default: it is
// intended for visualisers like us.
// Output: multicast to 239.255.<hi>.<lo> or unicast, with our own CID, source name and
// priority; on stop() every universe we sent gets the three "stream terminated" packets
// the standard asks for.

#include "dmx/DmxInterface.h"
#include "dmx/platform/NetworkInterfaces.h"
#include "dmx/platform/UdpSocket.h"
#include "dmx/protocol/Sacn.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace dmxviz::dmx {

struct SacnConfig {
    Ipv4Address nic;                            // 0.0.0.0 = all interfaces
    std::uint16_t port = sacn::kDefaultPort;    // listen port (0 = any free port) and output port
    int universeOffset = 0;                     // logical universe = sACN universe + offset
    std::vector<std::uint16_t> universes{1};    // sACN universes to receive
    bool multicastInput = true;                 // join the multicast group of each universe
    bool acceptAllUniverses = false;            // also accept universes not listed (e.g. via unicast)
    bool acceptPreview = true;

    std::string sourceName = "DmxViz";
    std::uint8_t priority = sacn::kDefaultPriority;  // 0..200
    bool multicastOutput = true;                // false: unicast to unicastTargets only
    std::vector<Endpoint> unicastTargets;
    sacn::Cid cid{};                            // our identity as a sender; all zero = keep the current one
};

class SacnInterface : public DmxInterface {
public:
    static constexpr const char* kTypeName = "sACN";

    SacnInterface();  // generates a fresh CID
    ~SacnInterface() override;

    std::string typeName() const override { return kTypeName; }
    Capabilities caps() const override { return {true, true}; }
    bool start(std::string& error) override;
    void stop() override;
    void send(UniverseId universe, const UniverseData& data) override;
    nlohmann::json saveConfig() const override;
    bool loadConfig(const nlohmann::json& settings, std::string& error) override;
    std::string summary() const override;

    const SacnConfig& config() const { return config_; }
    // Applies at the next start(). An all-zero CID in `newConfig` keeps the interface's CID.
    void setConfig(SacnConfig newConfig);
    std::uint16_t boundPort() const { return localPort_.load(); }

private:
    struct SequenceState {
        sacn::Cid cid{};
        std::uint16_t universe = 0;
        std::uint8_t last = 0;
        TimePoint lastSeen{};
    };

    void run();
    void handlePacket(std::span<const std::uint8_t> bytes, const Endpoint& from);
    bool acceptSequence(const sacn::Cid& cid, std::uint16_t universe, std::uint8_t sequence, TimePoint now);
    void forgetSequence(const sacn::Cid& cid, std::uint16_t universe);
    void sendPacket(std::uint16_t universe, std::span<const std::uint8_t> slots, bool terminated);
    void sendTerminations();

    SacnConfig config_;  // edited by the UI

    // Valid between start() and stop().
    SacnConfig active_;
    std::uint16_t destinationPort_ = sacn::kDefaultPort;
    UdpSocket receiveSocket_;
    std::atomic<std::uint16_t> localPort_{0};
    std::thread thread_;
    std::atomic<bool> stopRequested_{false};

    // IO thread only.
    std::vector<bool> subscribed_;          // indexed by sACN universe
    std::vector<SequenceState> sequences_;  // last sequence number per (sender, universe)
    std::array<std::uint8_t, 2048> receiveBuffer_{};

    // Output, guarded by sendMutex_.
    std::mutex sendMutex_;
    UdpSocket sendSocket_;
    std::array<std::uint8_t, sacn::kMaxDataPacketSize> sendBuffer_{};
    std::vector<std::uint8_t> sendSequence_;  // indexed by sACN universe
    std::vector<bool> sentUniverses_;         // universes to terminate on stop()
};

}  // namespace dmxviz::dmx
