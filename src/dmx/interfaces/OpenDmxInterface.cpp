#include "dmx/interfaces/OpenDmxInterface.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <format>
#include <thread>

namespace dmxviz::dmx {
namespace {

using namespace std::chrono_literals;

constexpr auto kBreak = 110us;          // DMX512 minimum is 92 us
constexpr auto kMarkAfterBreak = 16us;  // minimum 12 us
constexpr auto kMaxSleepStep = 20ms;    // keep stop() responsive at low refresh rates

}  // namespace

OpenDmxInterface::~OpenDmxInterface() {
    stop();
}

void OpenDmxInterface::setRefreshRate(int hz) {
    refreshRate_.store(std::clamp(hz, kMinRefreshRate, kMaxRefreshRate));
}

bool OpenDmxInterface::onPortOpened(SerialPort& /*port*/) {
    period_ = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / refreshRate_.load()));
    nextFrame_ = Clock::now();
    packet_[0] = 0;  // start code
    return true;
}

void OpenDmxInterface::send(UniverseId /*universe*/, const UniverseData& data) {
    std::lock_guard lock(frameMutex_);
    frame_ = data;
}

bool OpenDmxInterface::ioStep(SerialPort& port) {
    // Wait for the next frame slot in short steps so stop() is never delayed long.
    for (TimePoint now = Clock::now(); now < nextFrame_; now = Clock::now()) {
        if (stopRequested()) return true;
        std::this_thread::sleep_for(std::min<Clock::duration>(nextFrame_ - now, kMaxSleepStep));
    }

    {
        std::lock_guard lock(frameMutex_);
        std::copy(frame_.begin(), frame_.end(), packet_.begin() + 1);
    }
    if (!port.setBreak(true)) return false;
    std::this_thread::sleep_for(kBreak);
    if (!port.setBreak(false)) return false;
    std::this_thread::sleep_for(kMarkAfterBreak);
    if (!port.write(packet_) || !port.drain()) return false;
    countOut();

    // Schedule the next frame; if we fell behind (slow driver), restart from now
    // instead of sending a burst to catch up.
    nextFrame_ += period_;
    const TimePoint now = Clock::now();
    if (nextFrame_ < now) nextFrame_ = now;
    return true;
}

nlohmann::json OpenDmxInterface::saveConfig() const {
    return {{"port", portPath()}, {"refreshRate", refreshRate_.load()}};
}

bool OpenDmxInterface::loadConfig(const nlohmann::json& settings, std::string& error) {
    try {
        setPortPath(settings.value("port", std::string()));
        setRefreshRate(settings.value("refreshRate", 30));
        return true;
    } catch (const nlohmann::json::exception& e) {
        error = std::format("Open DMX settings: {}", e.what());
        return false;
    }
}

}  // namespace dmxviz::dmx
