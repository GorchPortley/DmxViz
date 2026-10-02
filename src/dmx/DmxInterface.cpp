#include "dmx/DmxInterface.h"

#include "core/Log.h"

#include <format>

namespace dmxviz::dmx {
namespace {

// "Art-Net 'Stage left'" or just "Art-Net" for log lines.
std::string logName(const std::string& type, const std::string& label) {
    return label.empty() ? type : std::format("{} '{}'", type, label);
}

}  // namespace

InterfaceStatus DmxInterface::status() const {
    InterfaceStatus s;
    s.state = state_.load();
    s.packetsIn = packetsIn_.load(std::memory_order_relaxed);
    s.packetsOut = packetsOut_.load(std::memory_order_relaxed);
    s.packetsInvalid = packetsInvalid_.load(std::memory_order_relaxed);
    const TimePoint now = Clock::now();
    std::lock_guard lock(statusMutex_);
    s.message = message_;
    s.packetsInPerSecond = inRate_.update(now, s.packetsIn);
    s.packetsOutPerSecond = outRate_.update(now, s.packetsOut);
    return s;
}

std::string DmxInterface::label() const {
    std::lock_guard lock(statusMutex_);
    return label_;
}

void DmxInterface::setLabel(std::string text) {
    std::lock_guard lock(statusMutex_);
    label_ = std::move(text);
}

void DmxInterface::setRunning(std::string message) {
    log::info("dmx", "{}: {}", logName(typeName(), label()), message);
    std::lock_guard lock(statusMutex_);
    message_ = std::move(message);
    state_.store(InterfaceState::Running);
}

void DmxInterface::setStopped() {
    std::lock_guard lock(statusMutex_);
    message_ = "stopped";
    state_.store(InterfaceState::Stopped);
}

void DmxInterface::setError(std::string message) {
    log::warn("dmx", "{}: {}", logName(typeName(), label()), message);
    std::lock_guard lock(statusMutex_);
    message_ = std::move(message);
    state_.store(InterfaceState::Error);
}

void DmxInterface::submitInput(UniverseId universe, const SourceDescriptor& source,
                               std::span<const std::uint8_t> slots) {
    countIn();
    if (!store_ || !inputEnabled_.load(std::memory_order_relaxed)) return;
    store_->submit(universe, source, slots);
}

void DmxInterface::removeInput(UniverseId universe, const SourceId& source) {
    countIn();
    if (store_) store_->removeSource(universe, source);
}

void DmxInterface::releaseInputs() {
    if (store_) store_->removeInterfaceSources(id_);
}

}  // namespace dmxviz::dmx
