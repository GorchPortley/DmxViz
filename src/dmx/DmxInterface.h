#pragma once
// DmxInterface: base class for one configured DMX port – a network protocol on a NIC
// (Art-Net, sACN), a USB widget (Enttec Pro, Open DMX) or the loopback.
//
// Lifecycle: DmxManager creates an interface through the InterfaceRegistry, attach()es
// it to the UniverseStore, loads its configuration and calls start(). start() opens the
// socket or serial port and spawns the interface's own IO thread; failures (port in use,
// device missing) come back as `false` + error text, never as exceptions. stop() joins
// the IO thread quickly (well under 200 ms) and releases the interface's sources in the
// store.
//
// Threads: configuration setters belong to the main thread and take effect at the next
// start(). Received frames go straight from the IO thread into the store. send(),
// status() and setInputEnabled() may be called from any thread.

#include "dmx/DmxTypes.h"
#include "dmx/RateMeter.h"
#include "dmx/UniverseStore.h"

#include <nlohmann/json_fwd.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>

namespace dmxviz::dmx {

struct Capabilities {
    bool input = false;   // can receive DMX into the store
    bool output = false;  // can send DMX
};

enum class InterfaceState { Stopped, Running, Error };

struct InterfaceStatus {
    InterfaceState state = InterfaceState::Stopped;
    std::string message;             // e.g. "listening on 0.0.0.0:6454", or the last error
    std::uint64_t packetsIn = 0;     // valid DMX/protocol packets received
    std::uint64_t packetsOut = 0;    // packets sent
    std::uint64_t packetsInvalid = 0;  // malformed, rejected or out-of-sequence packets
    float packetsInPerSecond = 0.0f;
    float packetsOutPerSecond = 0.0f;
};

class DmxInterface {
public:
    virtual ~DmxInterface() = default;

    // "Art-Net", "sACN", ... – identical to the registry name of the type.
    virtual std::string typeName() const = 0;
    virtual Capabilities caps() const = 0;
    virtual bool start(std::string& error) = 0;
    virtual void stop() = 0;
    // Output: hands one universe to the interface. Must not block for long (UDP send or a
    // hand-over to the IO thread). Ignored while stopped or for input-only interfaces.
    virtual void send(UniverseId universe, const UniverseData& data) = 0;
    // Type-specific settings. Common fields (label, enabled, input) are saved by DmxManager.
    virtual nlohmann::json saveConfig() const = 0;
    virtual bool loadConfig(const nlohmann::json& config, std::string& error) = 0;
    // One line for lists, e.g. "Art-Net on 192.168.1.10:6454".
    virtual std::string summary() const { return typeName(); }

    InterfaceStatus status() const;
    InterfaceState state() const { return state_.load(); }
    bool running() const { return state_.load() == InterfaceState::Running; }

    // Set by DmxManager (or a test) before start().
    void attach(UniverseStore* targetStore, InterfaceId interfaceId) {
        store_ = targetStore;
        id_ = interfaceId;
    }
    InterfaceId id() const { return id_; }

    // User-visible name; also used as the source name of USB inputs.
    std::string label() const;
    void setLabel(std::string text);

    // Whether the user wants this interface running (persisted; DmxManager starts it at load).
    bool enabled() const { return enabled_; }
    void setEnabled(bool on) { enabled_ = on; }

    // Received data is dropped while input is disabled (takes effect immediately).
    bool inputEnabled() const { return inputEnabled_.load(); }
    void setInputEnabled(bool on) { inputEnabled_.store(on); }

protected:
    void setRunning(std::string message);
    void setStopped();
    void setError(std::string message);

    void countIn() { packetsIn_.fetch_add(1, std::memory_order_relaxed); }
    void countOut(std::uint64_t n = 1) { packetsOut_.fetch_add(n, std::memory_order_relaxed); }
    void countInvalid() { packetsInvalid_.fetch_add(1, std::memory_order_relaxed); }

    // Hands a received frame to the store (unless input is disabled) and counts it.
    void submitInput(UniverseId universe, const SourceDescriptor& source, std::span<const std::uint8_t> slots);
    // The sACN stream-terminated case.
    void removeInput(UniverseId universe, const SourceId& source);
    // Drops everything this interface contributed to the store (call from stop()).
    void releaseInputs();

    UniverseStore* store() const { return store_; }

private:
    UniverseStore* store_ = nullptr;
    InterfaceId id_ = 0;
    bool enabled_ = false;
    std::atomic<bool> inputEnabled_{true};

    std::atomic<InterfaceState> state_{InterfaceState::Stopped};
    std::atomic<std::uint64_t> packetsIn_{0};
    std::atomic<std::uint64_t> packetsOut_{0};
    std::atomic<std::uint64_t> packetsInvalid_{0};
    mutable std::mutex statusMutex_;  // guards label_, message_ and the rate samplers
    std::string label_;
    std::string message_;
    mutable CounterRate inRate_;
    mutable CounterRate outRate_;
};

}  // namespace dmxviz::dmx
