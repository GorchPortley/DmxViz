#include "dmx/interfaces/EnttecProInterface.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <format>

namespace dmxviz::dmx {
namespace {

using namespace std::chrono_literals;

// Short, so queued output goes out quickly even while no input arrives.
constexpr auto kReadTimeout = 10ms;

}  // namespace

EnttecProInterface::~EnttecProInterface() { stop(); }

SerialSettings EnttecProInterface::serialSettings() const {
    // The widget talks USB; the baud rate of the virtual COM port is irrelevant.
    return SerialSettings{57600, false};
}

bool EnttecProInterface::onPortOpened(SerialPort& port) {
    parser_.reset();
    inputFrame_.fill(0);
    sourceName_ = label().empty() ? std::string(kTypeName) : label();
    {
        // After a reconnect, resend the last output (if we ever sent any: a widget used
        // purely as an input must not be switched to output).
        std::lock_guard lock(outputMutex_);
        outputPending_ = outputPending_ || everSent_;
    }
    port.discardBuffers();
    // Ask for every received frame (label 5) rather than only changes.
    const std::size_t size = enttec::encodeReceiveDmxOnChange(false, writeBuffer_);
    return port.write(std::span(writeBuffer_).first(size));
}

void EnttecProInterface::send(UniverseId /*universe*/, const UniverseData& data) {
    // The widget has one output: whatever universe is routed here goes out.
    if (state() == InterfaceState::Stopped) return;
    std::lock_guard lock(outputMutex_);
    pendingOutput_ = data;
    outputPending_ = true;
    everSent_ = true;
}

bool EnttecProInterface::ioStep(SerialPort& port) {
    bool haveOutput = false;
    {
        std::lock_guard lock(outputMutex_);
        if (outputPending_) {
            outputFrame_ = pendingOutput_;
            outputPending_ = false;
            haveOutput = true;
        }
    }
    if (haveOutput) {
        const std::size_t size = enttec::encodeSendDmx(outputFrame_, writeBuffer_);
        if (!port.write(std::span(writeBuffer_).first(size))) return false;
        countOut();
    }

    const int received = port.read(readBuffer_, kReadTimeout);
    if (received < 0) return false;
    parser_.feed(std::span(readBuffer_).first(static_cast<std::size_t>(received)),
                 [this](const enttec::WidgetMessage& message) { handleMessage(message); });
    return true;
}

void EnttecProInterface::handleMessage(const enttec::WidgetMessage& message) {
    if (message.label == enttec::label::kReceivedDmx) {
        const auto dmx = enttec::decodeReceivedDmx(message);
        if (!dmx || !dmx->valid()) {
            countInvalid();  // malformed, or the widget reported an overrun
            return;
        }
        if (dmx->startCode != 0) return;  // not dimmer data
        const std::size_t n = std::min(dmx->channels.size(), kUniverseSize);
        std::copy_n(dmx->channels.begin(), n, inputFrame_.begin());
        std::fill(inputFrame_.begin() + static_cast<std::ptrdiff_t>(n), inputFrame_.end(), std::uint8_t{0});
        submitFrame();
    } else if (message.label == enttec::label::kReceivedDmxChange) {
        const auto change = enttec::decodeChangeOfState(message);
        if (!change || !enttec::applyChangeOfState(*change, inputFrame_)) {
            countInvalid();
            return;
        }
        submitFrame();
    }
    // Other labels are replies to requests we never make.
}

void EnttecProInterface::submitFrame() {
    SourceDescriptor source;
    source.id = SourceId{id(), {}};
    source.protocol = Protocol::EnttecPro;
    source.name = sourceName_;
    submitInput(inputUniverse_.load(), source, inputFrame_);
}

nlohmann::json EnttecProInterface::saveConfig() const {
    return {{"port", portPath()}, {"inputUniverse", inputUniverse()}};
}

bool EnttecProInterface::loadConfig(const nlohmann::json& settings, std::string& error) {
    try {
        const int universe = settings.value("inputUniverse", 1);
        if (universe < 1 || universe > 0xFFFF) {
            error = std::format("invalid input universe {}", universe);
            return false;
        }
        setPortPath(settings.value("port", std::string()));
        setInputUniverse(static_cast<UniverseId>(universe));
        return true;
    } catch (const nlohmann::json::exception& e) {
        error = std::format("Enttec Pro settings: {}", e.what());
        return false;
    }
}

}  // namespace dmxviz::dmx
