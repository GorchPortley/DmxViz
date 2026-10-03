#include "dmx/interfaces/LoopbackInterface.h"

#include "dmx/interfaces/ConfigJson.h"

#include <nlohmann/json.hpp>

#include <format>

namespace dmxviz::dmx {

bool LoopbackInterface::start(std::string& /*error*/) {
    std::string name = label().empty() ? std::string(kTypeName) : label();
    {
        std::lock_guard lock(nameMutex_);
        sourceName_ = std::move(name);
    }
    setRunning(std::format("looping universes back with offset {}", universeOffset_.load()));
    return true;
}

void LoopbackInterface::stop() {
    setStopped();
    releaseInputs();
}

void LoopbackInterface::send(UniverseId universe, const UniverseData& data) {
    if (!running()) return;
    countOut();
    const int target = int{universe} + universeOffset_.load();
    if (target < 1 || target > 0xFFFF) {
        countInvalid();
        return;
    }
    SourceDescriptor source;
    source.id = SourceId{id(), {}};
    source.protocol = Protocol::Loopback;
    std::lock_guard lock(nameMutex_);  // source.name is a view of sourceName_, so hold it while submitting
    source.name = sourceName_;
    submitInput(static_cast<UniverseId>(target), source, data);
}

nlohmann::json LoopbackInterface::saveConfig() const {
    return {{"universeOffset", universeOffset_.load()}};
}

bool LoopbackInterface::loadConfig(const nlohmann::json& settings, std::string& error) {
    try {
        int offset = 0;
        if (!config::readInt(settings, "universeOffset", -config::kMaxUniverseOffset, config::kMaxUniverseOffset,
                             offset, error))
            return false;
        universeOffset_.store(offset);
        return true;
    } catch (const nlohmann::json::exception& e) {
        error = e.what();
        return false;
    }
}

std::string LoopbackInterface::summary() const {
    const int offset = universeOffset_.load();
    return offset == 0 ? std::string("Loopback") : std::format("Loopback (offset {:+})", offset);
}

}  // namespace dmxviz::dmx
