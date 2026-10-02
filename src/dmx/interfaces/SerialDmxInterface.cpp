#include "dmx/interfaces/SerialDmxInterface.h"

#include <chrono>
#include <format>
#include <system_error>

namespace dmxviz::dmx {
namespace {

using namespace std::chrono_literals;

constexpr auto kReconnectInterval = 1s;
constexpr auto kIdleStep = 50ms;  // stop-flag polling while waiting to reconnect

}  // namespace

bool SerialDmxInterface::start(std::string& error) {
    if (running()) return true;
    stop();

    activePortPath_ = portPath_;
    if (activePortPath_.empty()) {
        error = "no serial port selected";
        setError(error);
        return false;
    }
    if (!openPort(error)) {
        setError(error);
        return false;
    }

    stopRequested_ = false;
    try {
        thread_ = std::thread(&SerialDmxInterface::run, this);
    } catch (const std::system_error& e) {
        error = std::format("cannot start IO thread: {}", e.what());
        port_.close();
        setError(error);
        return false;
    }
    setRunning(std::format("open on {}", activePortPath_));
    return true;
}

void SerialDmxInterface::stop() {
    stopRequested_ = true;
    if (thread_.joinable()) thread_.join();
    port_.close();
    releaseInputs();
    if (state() != InterfaceState::Stopped) setStopped();
}

std::string SerialDmxInterface::summary() const {
    return portPath_.empty() ? typeName() : std::format("{} on {}", typeName(), portPath_);
}

bool SerialDmxInterface::openPort(std::string& error) {
    if (!port_.open(activePortPath_, serialSettings(), error)) return false;
    if (!onPortOpened(port_)) {
        error = std::format("{}: {}", activePortPath_, port_.lastError());
        port_.close();
        return false;
    }
    return true;
}

void SerialDmxInterface::run() {
    TimePoint nextReconnect{};
    while (!stopRequested_.load()) {
        if (!port_.isOpen()) {
            if (Clock::now() < nextReconnect) {
                std::this_thread::sleep_for(kIdleStep);
                continue;
            }
            std::string error;
            if (openPort(error)) {
                setRunning(std::format("reconnected on {}", activePortPath_));
            } else {
                nextReconnect = Clock::now() + kReconnectInterval;
            }
            continue;
        }
        if (!ioStep(port_)) {
            // Typically the USB device was unplugged. Keep trying until stop().
            setError(std::format("{}: {} (reconnecting)", activePortPath_, port_.lastError()));
            port_.close();
            nextReconnect = Clock::now() + kReconnectInterval;
        }
    }
}

}  // namespace dmxviz::dmx
