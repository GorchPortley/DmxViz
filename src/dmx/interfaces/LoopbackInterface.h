#pragma once
// LoopbackInterface: whatever is sent to it comes straight back as input.
//
// Useful for tests (output routing without a network) and for feeding the test console
// into another logical universe: output universe U re-appears as input universe
// U + offset. With offset 0 the loop is harmless because DmxManager never sends an
// interface its own input back (see UniverseStore::mergeForOutput).

#include "dmx/DmxInterface.h"

#include <algorithm>
#include <atomic>
#include <mutex>

namespace dmxviz::dmx {

class LoopbackInterface : public DmxInterface {
public:
    static constexpr const char* kTypeName = "Loopback";

    std::string typeName() const override { return kTypeName; }
    Capabilities caps() const override { return {true, true}; }
    bool start(std::string& error) override;
    void stop() override;
    void send(UniverseId universe, const UniverseData& data) override;
    nlohmann::json saveConfig() const override;
    bool loadConfig(const nlohmann::json& settings, std::string& error) override;
    std::string summary() const override;

    int universeOffset() const { return universeOffset_.load(); }
    // Atomic: the output thread reads it in send() while the UI may edit it.
    void setUniverseOffset(int offset) { universeOffset_.store(std::clamp(offset, -0xFFFF, 0xFFFF)); }

private:
    std::atomic<int> universeOffset_{0};
    std::mutex nameMutex_;    // guards sourceName_ (written by start(), read by send() on the output thread)
    std::string sourceName_;  // copy of the label taken at start()
};

}  // namespace dmxviz::dmx
