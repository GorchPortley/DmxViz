#pragma once
// OpenDmxInterface: Enttec Open DMX USB and other "dumb" FTDI cables, output only.
//
// These adapters have no processor: the PC must generate the DMX signal itself, frame
// after frame – a break (line held low >= 92 us), a mark-after-break (>= 12 us), then
// start code 0 and 512 slots at 250000 baud, 8N2. The IO thread does exactly that at a
// fixed refresh rate (default 30 Hz). Timing is best effort: the OS sleeps longer than
// asked for, which only lengthens break and mark – still valid DMX.

#include "dmx/interfaces/SerialDmxInterface.h"

#include <array>
#include <atomic>
#include <mutex>

namespace dmxviz::dmx {

class OpenDmxInterface : public SerialDmxInterface {
public:
    static constexpr const char* kTypeName = "Enttec Open DMX USB";
    static constexpr int kMinRefreshRate = 1;
    static constexpr int kMaxRefreshRate = 40;  // a full frame takes ~23 ms on the wire

    ~OpenDmxInterface() override;

    std::string typeName() const override { return kTypeName; }
    Capabilities caps() const override { return {false, true}; }
    void send(UniverseId universe, const UniverseData& data) override;
    nlohmann::json saveConfig() const override;
    bool loadConfig(const nlohmann::json& settings, std::string& error) override;

    int refreshRate() const { return refreshRate_.load(); }
    void setRefreshRate(int hz);  // 1..40 Hz, applies at the next start()

protected:
    SerialSettings serialSettings() const override { return SerialSettings{250000, true}; }
    bool onPortOpened(SerialPort& port) override;
    bool ioStep(SerialPort& port) override;

private:
    std::atomic<int> refreshRate_{30};

    // IO thread only.
    TimePoint nextFrame_{};
    Clock::duration period_{};
    std::array<std::uint8_t, 1 + kUniverseSize> packet_{};  // start code + slots

    std::mutex frameMutex_;
    UniverseData frame_{};  // latest data from send()
};

}  // namespace dmxviz::dmx
