#pragma once
// DmxManager: the DMX module's front door for the app and the UI.
//
// It owns
//   * the UniverseStore every input writes into,
//   * the InterfaceRegistry (which interface types exist) and the configured interfaces,
//   * output routing: which logical universes are sent through which output interface.
//     A dedicated output thread refreshes the routes at a fixed rate (default 40 Hz),
//     sending a universe when it changed and at least once per second as keep-alive,
//   * the programmer (test console) API, and
//   * JSON save/load of the whole DMX configuration.
//
// Threads: everything except processOutput() is meant for the main thread. Interfaces
// receive on their own IO threads and the output thread only reads the store and calls
// DmxInterface::send(). Interfaces are only created and destroyed by the main thread, so
// pointers returned by findInterface()/interfaces() stay valid until removeInterface()
// or loadConfig().

#include "dmx/DmxInterface.h"
#include "dmx/DmxSnapshot.h"
#include "dmx/InterfaceRegistry.h"
#include "dmx/UniverseStore.h"

#include <nlohmann/json_fwd.hpp>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace dmxviz::dmx {

// "Send logical universe `universe` through interface `interfaceId`."
struct OutputRoute {
    UniverseId universe = 1;
    InterfaceId interfaceId = 0;
    bool operator==(const OutputRoute&) const = default;
};

struct DmxManagerOptions {
    bool startOutputThread = true;  // tests turn it off and call processOutput() themselves
};

class DmxManager {
public:
    static constexpr double kDefaultOutputRate = 40.0;
    static constexpr double kMinOutputRate = 1.0;
    static constexpr double kMaxOutputRate = 44.0;  // the DMX512 line maximum

    explicit DmxManager(DmxManagerOptions options = {});
    ~DmxManager();  // stops the output thread and every interface
    DmxManager(const DmxManager&) = delete;
    DmxManager& operator=(const DmxManager&) = delete;

    // ---- interface types ------------------------------------------------------------
    const InterfaceRegistry& registry() const { return registry_; }
    InterfaceRegistry& registry() { return registry_; }

    // ---- configured interfaces --------------------------------------------------------
    // Creates a stopped interface of a registered type; returns its id, or 0 for an unknown type.
    InterfaceId addInterface(std::string_view typeName);
    InterfaceId addInterface(std::unique_ptr<DmxInterface> iface);
    // Stops and deletes the interface and drops its routes.
    bool removeInterface(InterfaceId id);
    DmxInterface* findInterface(InterfaceId id) const;
    template <typename T>
    T* findInterfaceAs(InterfaceId id) const {
        return dynamic_cast<T*>(findInterface(id));
    }
    std::vector<DmxInterface*> interfaces() const;  // in the order they were added

    // Starting marks an interface enabled (saved in the config), stopping disables it.
    bool startInterface(InterfaceId id, std::string& error);
    void stopInterface(InterfaceId id);
    // Starts every interface marked enabled (e.g. after loadConfig() at program start).
    // Returns the number that failed; their status() says why.
    int startEnabledInterfaces();
    void stopAllInterfaces();

    // ---- output routing -----------------------------------------------------------------
    std::vector<OutputRoute> routes() const;
    void setRoutes(const std::vector<OutputRoute>& newRoutes);
    void addRoute(const OutputRoute& route);  // ignores duplicates
    void removeRoute(const OutputRoute& route);
    // Master switch for all DMX output (the test console's "output enable").
    void setOutputEnabled(bool on) { outputEnabled_.store(on); }
    bool outputEnabled() const { return outputEnabled_.load(); }
    void setOutputRate(double hz);  // clamped to 1..44
    double outputRate() const { return outputRate_.load(); }
    // One output refresh: sends each routed universe that changed since its last send,
    // or that was last sent a second ago or more. The output thread calls this; tests
    // call it with their own clock.
    void processOutput(TimePoint now = Clock::now());

    // ---- programmer (test console) --------------------------------------------------------
    void setProgrammerChannel(UniverseId universe, std::uint16_t address1Based, std::uint8_t value);
    void setProgrammerUniverse(UniverseId universe, const UniverseData& values);
    void clearProgrammerUniverse(UniverseId universe);
    void clearProgrammer();
    void setProgrammerMode(ProgrammerMode mode);
    ProgrammerMode programmerMode() const;
    std::optional<UniverseData> programmerValues(UniverseId universe) const;

    // ---- data for the main thread ---------------------------------------------------------
    // Fills `out` with all merged universes; call once per frame and reuse `out`.
    void snapshot(DmxSnapshot& out, TimePoint now = Clock::now());
    UniverseStore& store() { return store_; }

    // ---- configuration ----------------------------------------------------------------------
    nlohmann::json saveConfig() const;
    // Replaces all interfaces, routes and settings. Nothing changes if the JSON is invalid.
    // Interfaces are created stopped: call startEnabledInterfaces() afterwards.
    bool loadConfig(const nlohmann::json& config, std::string& error);

private:
    struct RouteState {
        OutputRoute route;
        UniverseData lastSent{};
        TimePoint lastSentTime{};
        bool sentOnce = false;
    };

    void outputLoop();
    DmxInterface* findLocked(InterfaceId id) const;

    UniverseStore store_;
    InterfaceRegistry registry_;

    mutable std::mutex mutex_;  // guards interfaces_, routes_, nextId_, scratch_
    std::vector<std::unique_ptr<DmxInterface>> interfaces_;
    std::vector<RouteState> routes_;
    InterfaceId nextId_ = 1;
    UniverseData scratch_{};

    std::atomic<bool> outputEnabled_{true};
    std::atomic<double> outputRate_{kDefaultOutputRate};

    std::thread outputThread_;
    std::mutex threadMutex_;
    std::condition_variable threadWake_;
    bool stopThread_ = false;
};

}  // namespace dmxviz::dmx
