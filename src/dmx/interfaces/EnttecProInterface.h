#pragma once
// EnttecProInterface: an Enttec DMX USB Pro (or compatible) widget, input and output.
//
// The widget has its own processor: we send it a whole universe (label 6) whenever the
// output changes and it keeps refreshing the DMX line by itself. For input we switch it
// to "send every received frame" (label 8 with 0) and decode label 5 packets; label 9
// change-of-state packets are applied too, should a widget be in that mode.
// One thread does both reading and writing, because on Windows a synchronous serial
// handle cannot read and write concurrently anyway.

#include "dmx/interfaces/SerialDmxInterface.h"
#include "dmx/protocol/EnttecPro.h"

#include <array>
#include <mutex>

namespace dmxviz::dmx {

class EnttecProInterface : public SerialDmxInterface {
public:
    static constexpr const char* kTypeName = "Enttec DMX USB Pro";

    ~EnttecProInterface() override;

    std::string typeName() const override { return kTypeName; }
    Capabilities caps() const override { return {true, true}; }
    void send(UniverseId universe, const UniverseData& data) override;
    nlohmann::json saveConfig() const override;
    bool loadConfig(const nlohmann::json& settings, std::string& error) override;

    // Logical universe that DMX received by the widget is stored in.
    UniverseId inputUniverse() const { return inputUniverse_.load(); }
    void setInputUniverse(UniverseId universe) { inputUniverse_.store(universe); }

protected:
    SerialSettings serialSettings() const override;
    bool onPortOpened(SerialPort& port) override;
    bool ioStep(SerialPort& port) override;

private:
    void handleMessage(const enttec::WidgetMessage& message);
    void submitFrame();

    std::atomic<UniverseId> inputUniverse_{1};

    // IO thread only.
    enttec::WidgetParser parser_;
    UniverseData inputFrame_{};
    std::string sourceName_;
    std::array<std::uint8_t, 1024> readBuffer_{};
    std::array<std::uint8_t, enttec::kMaxMessageSize> writeBuffer_{};
    UniverseData outputFrame_{};

    // Hand-over from send() to the IO thread.
    std::mutex outputMutex_;
    UniverseData pendingOutput_{};
    bool outputPending_ = false;
    bool everSent_ = false;
};

}  // namespace dmxviz::dmx
