#include "dmx/interfaces/LoopbackInterface.h"

#include <nlohmann/json.hpp>

#include <format>

namespace dmxviz::dmx {

bool LoopbackInterface::start(std::string& /*error*/) {
    sourceName_ = label().empty() ? std::string(kTypeName) : label();
    setRunning(std::format("looping universes back with offset {}", universeOffset_));
    return true;
}

void LoopbackInterface::stop() {
    setStopped();
    releaseInputs();
}

void LoopbackInterface::send(UniverseId universe, const UniverseData& data) {
    if (!running()) return;
    countOut();
    const int target = int{universe} + universeOffset_;
    if (target < 1 || target > 0xFFFF) {
        countInvalid();
        return;
    }
    SourceDescriptor source;
    source.id = SourceId{id(), {}};
    source.protocol = Protocol::Loopback;
    source.name = sourceName_;
    submitInput(static_cast<UniverseId>(target), source, data);
}

nlohmann::json LoopbackInterface::saveConfig() const { return {{"universeOffset", universeOffset_}}; }

bool LoopbackInterface::loadConfig(const nlohmann::json& config, std::string& error) {
    try {
        universeOffset_ = config.value("universeOffset", 0);
        return true;
    } catch (const nlohmann::json::exception& e) {
        error = e.what();
        return false;
    }
}

std::string LoopbackInterface::summary() const {
    return universeOffset_ == 0 ? std::string("Loopback") : std::format("Loopback (offset {:+})", universeOffset_);
}

}  // namespace dmxviz::dmx
