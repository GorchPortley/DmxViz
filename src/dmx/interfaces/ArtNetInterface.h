#pragma once
// ArtNetInterface: Art-Net 4 input and output on one network interface.
//
// Input: listens on UDP 6454 (configurable), maps each ArtDmx port-address to logical
// universe "port-address + 1 + universeOffset" and answers ArtPoll so consoles list
// DmxViz as a visualiser node and send to it.
// Output: ArtDmx to the NIC's broadcast address or to a list of unicast targets.
//
// NIC choice: 0.0.0.0 means all interfaces. For a specific NIC we still bind 0.0.0.0,
// because Linux only delivers broadcast packets to sockets bound to the wildcard
// address, and drop packets from other subnets instead. Loopback addresses
// (127.x.x.x) are bound directly, which suits tests and same-PC setups.

#include "dmx/DmxInterface.h"
#include "dmx/platform/NetworkInterfaces.h"
#include "dmx/platform/UdpSocket.h"
#include "dmx/protocol/ArtNet.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace dmxviz::dmx {

struct ArtNetConfig {
    Ipv4Address nic;                            // 0.0.0.0 = all interfaces
    std::uint16_t port = artnet::kDefaultPort;  // listen port (0 = any free port) and output port
    int universeOffset = 0;                     // logical universe = port-address + 1 + offset
    bool broadcastOutput = true;                // false: send to unicastTargets only
    std::vector<Endpoint> unicastTargets;
    bool replyToPoll = true;
    std::vector<UniverseId> announcedUniverses{1, 2, 3, 4};  // logical universes listed in ArtPollReply
    std::string shortName = "DmxViz";
    std::string longName = "DmxViz DMX visualiser";
};

class ArtNetInterface : public DmxInterface {
public:
    static constexpr const char* kTypeName = "Art-Net";

    ArtNetInterface();
    ~ArtNetInterface() override;

    std::string typeName() const override { return kTypeName; }
    Capabilities caps() const override { return {true, true}; }
    bool start(std::string& error) override;
    void stop() override;
    void send(UniverseId universe, const UniverseData& data) override;
    nlohmann::json saveConfig() const override;
    bool loadConfig(const nlohmann::json& settings, std::string& error) override;
    std::string summary() const override;

    const ArtNetConfig& config() const { return config_; }
    void setConfig(ArtNetConfig newConfig) { config_ = std::move(newConfig); }  // applies at the next start()
    // The UDP port actually bound while running (useful when config().port is 0).
    std::uint16_t boundPort() const { return localPort_.load(); }

private:
    void run();
    void handlePacket(std::span<const std::uint8_t> packet, const Endpoint& from);
    void handleDmx(std::span<const std::uint8_t> packet, const Endpoint& from);
    void handlePoll(std::span<const std::uint8_t> packet, const Endpoint& from);
    bool acceptSender(const Endpoint& from) const;
    bool isLocalAddress(Ipv4Address address) const;
    bool isOwnEcho(const Endpoint& from) const;
    const NetworkInterfaceInfo* replyInterfaceFor(Ipv4Address poller) const;
    void buildPollReplies();

    ArtNetConfig config_;  // edited by the UI

    // Valid between start() and stop(); read by the IO thread and send().
    ArtNetConfig active_;
    std::vector<NetworkInterfaceInfo> localInterfaces_;
    std::optional<NetworkInterfaceInfo> nic_;
    UdpSocket socket_;
    std::atomic<std::uint16_t> localPort_{0};
    std::thread thread_;
    std::atomic<bool> stopRequested_{false};

    // IO thread only.
    std::vector<artnet::PollReply> replyPages_;
    std::uint32_t pollReplyCount_ = 0;
    std::array<std::uint8_t, artnet::kPollReplySize> replyBuffer_{};
    std::array<std::uint8_t, 2048> receiveBuffer_{};

    // Output, guarded by sendMutex_.
    std::mutex sendMutex_;
    std::array<std::uint8_t, artnet::kMaxDmxPacketSize> sendBuffer_{};
    std::vector<std::uint8_t> sequence_;  // last sequence number per port-address
    Endpoint broadcastTarget_;
    std::atomic<Clock::rep> lastSendTicks_{0};  // to recognise our own broadcasts coming back
};

}  // namespace dmxviz::dmx
