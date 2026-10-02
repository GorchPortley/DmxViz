#include "dmx/DmxManager.h"

#include "core/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <format>
#include <set>
#include <system_error>

namespace dmxviz::dmx {
namespace {

using namespace std::chrono_literals;

constexpr auto kKeepAlive = 1s;  // unchanged universes are still sent this often
constexpr int kConfigVersion = 1;

const char* programmerModeName(ProgrammerMode mode) {
    return mode == ProgrammerMode::Override ? "override" : "merge";
}

}  // namespace

DmxManager::DmxManager(DmxManagerOptions options) : registry_(InterfaceRegistry::withBuiltinTypes()) {
    if (options.startOutputThread) {
        try {
            outputThread_ = std::thread(&DmxManager::outputLoop, this);
        } catch (const std::system_error& e) {
            log::error("dmx", "cannot start the DMX output thread: {}", e.what());
        }
    }
}

DmxManager::~DmxManager() {
    {
        std::lock_guard lock(threadMutex_);
        stopThread_ = true;
    }
    threadWake_.notify_all();
    if (outputThread_.joinable()) outputThread_.join();
    stopAllInterfaces();
}

// ---- interfaces ---------------------------------------------------------------------------

InterfaceId DmxManager::addInterface(std::string_view typeName) {
    std::unique_ptr<DmxInterface> iface = registry_.create(typeName);
    if (!iface) {
        log::warn("dmx", "unknown interface type '{}'", typeName);
        return 0;
    }
    return addInterface(std::move(iface));
}

InterfaceId DmxManager::addInterface(std::unique_ptr<DmxInterface> iface) {
    if (!iface) return 0;
    std::lock_guard lock(mutex_);
    const InterfaceId id = nextId_++;
    iface->attach(&store_, id);
    if (iface->label().empty()) iface->setLabel(std::format("{} {}", iface->typeName(), id));
    interfaces_.push_back(std::move(iface));
    return id;
}

bool DmxManager::removeInterface(InterfaceId id) {
    std::unique_ptr<DmxInterface> removed;
    {
        std::lock_guard lock(mutex_);
        const auto it = std::find_if(interfaces_.begin(), interfaces_.end(),
                                     [&](const std::unique_ptr<DmxInterface>& i) { return i->id() == id; });
        if (it == interfaces_.end()) return false;
        removed = std::move(*it);
        interfaces_.erase(it);
        std::erase_if(routes_, [&](const RouteState& r) { return r.route.interfaceId == id; });
    }
    removed->stop();  // outside the lock: the output thread can no longer see it
    return true;
}

DmxInterface* DmxManager::findLocked(InterfaceId id) const {
    for (const auto& iface : interfaces_)
        if (iface->id() == id) return iface.get();
    return nullptr;
}

DmxInterface* DmxManager::findInterface(InterfaceId id) const {
    std::lock_guard lock(mutex_);
    return findLocked(id);
}

std::vector<DmxInterface*> DmxManager::interfaces() const {
    std::lock_guard lock(mutex_);
    std::vector<DmxInterface*> result;
    for (const auto& iface : interfaces_) result.push_back(iface.get());
    return result;
}

bool DmxManager::startInterface(InterfaceId id, std::string& error) {
    DmxInterface* iface = findInterface(id);
    if (!iface) {
        error = std::format("no DMX interface with id {}", id);
        return false;
    }
    iface->setEnabled(true);
    return iface->start(error);
}

void DmxManager::stopInterface(InterfaceId id) {
    if (DmxInterface* iface = findInterface(id)) {
        iface->setEnabled(false);
        iface->stop();
    }
}

int DmxManager::startEnabledInterfaces() {
    int failures = 0;
    for (DmxInterface* iface : interfaces()) {
        if (!iface->enabled() || iface->running()) continue;
        std::string error;
        if (!iface->start(error)) ++failures;
    }
    return failures;
}

void DmxManager::stopAllInterfaces() {
    for (DmxInterface* iface : interfaces()) iface->stop();
}

// ---- output -------------------------------------------------------------------------------

std::vector<OutputRoute> DmxManager::routes() const {
    std::lock_guard lock(mutex_);
    std::vector<OutputRoute> result;
    for (const RouteState& r : routes_) result.push_back(r.route);
    return result;
}

void DmxManager::setRoutes(const std::vector<OutputRoute>& newRoutes) {
    std::lock_guard lock(mutex_);
    routes_.clear();
    for (const OutputRoute& route : newRoutes) {
        const bool duplicate =
            std::any_of(routes_.begin(), routes_.end(), [&](const RouteState& r) { return r.route == route; });
        if (!duplicate && route.universe != kInvalidUniverse) routes_.push_back(RouteState{route});
    }
}

void DmxManager::addRoute(const OutputRoute& route) {
    std::lock_guard lock(mutex_);
    if (route.universe == kInvalidUniverse) return;
    if (std::none_of(routes_.begin(), routes_.end(), [&](const RouteState& r) { return r.route == route; }))
        routes_.push_back(RouteState{route});
}

void DmxManager::removeRoute(const OutputRoute& route) {
    std::lock_guard lock(mutex_);
    std::erase_if(routes_, [&](const RouteState& r) { return r.route == route; });
}

void DmxManager::setOutputRate(double hz) {
    outputRate_.store(std::clamp(hz, kMinOutputRate, kMaxOutputRate));
}

void DmxManager::processOutput(TimePoint now) {
    if (!outputEnabled_.load()) return;
    std::lock_guard lock(mutex_);
    for (RouteState& state : routes_) {
        DmxInterface* iface = findLocked(state.route.interfaceId);
        if (!iface || !iface->caps().output || !iface->running()) {
            state.sentOnce = false;  // send at once when it comes (back) up
            continue;
        }
        // Never echo an interface's own input back to it.
        store_.mergeForOutput(state.route.universe, state.route.interfaceId, scratch_, now);
        const bool changed = !state.sentOnce || scratch_ != state.lastSent;
        if (!changed && now - state.lastSentTime < kKeepAlive) continue;
        iface->send(state.route.universe, scratch_);
        state.lastSent = scratch_;
        state.lastSentTime = now;
        state.sentOnce = true;
    }
}

void DmxManager::outputLoop() {
    TimePoint next = Clock::now();
    std::unique_lock lock(threadMutex_);
    while (!stopThread_) {
        next += std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / outputRate_.load()));
        const TimePoint now = Clock::now();
        if (next < now) next = now;  // fell behind: do not burst
        if (threadWake_.wait_until(lock, next, [this] { return stopThread_; })) break;
        lock.unlock();
        processOutput(Clock::now());
        lock.lock();
    }
}

// ---- programmer ---------------------------------------------------------------------------

void DmxManager::setProgrammerChannel(UniverseId universe, std::uint16_t address1Based, std::uint8_t value) {
    store_.setProgrammerChannel(universe, address1Based, value);
}

void DmxManager::setProgrammerUniverse(UniverseId universe, const UniverseData& values) {
    store_.setProgrammerUniverse(universe, values);
}

void DmxManager::clearProgrammerUniverse(UniverseId universe) {
    store_.clearProgrammerUniverse(universe);
}

void DmxManager::clearProgrammer() {
    store_.clearProgrammer();
}

void DmxManager::setProgrammerMode(ProgrammerMode mode) {
    store_.setProgrammerMode(mode);
}

ProgrammerMode DmxManager::programmerMode() const {
    return store_.programmerMode();
}

std::optional<UniverseData> DmxManager::programmerValues(UniverseId universe) const {
    return store_.programmerValues(universe);
}

void DmxManager::snapshot(DmxSnapshot& out, TimePoint now) {
    store_.snapshot(out, now);
}

// ---- configuration ------------------------------------------------------------------------

nlohmann::json DmxManager::saveConfig() const {
    nlohmann::json interfaceList = nlohmann::json::array();
    nlohmann::json routeList = nlohmann::json::array();
    {
        std::lock_guard lock(mutex_);
        for (const auto& iface : interfaces_) {
            interfaceList.push_back({
                {"id", iface->id()},
                {"type", iface->typeName()},
                {"label", iface->label()},
                {"enabled", iface->enabled()},
                {"inputEnabled", iface->inputEnabled()},
                {"settings", iface->saveConfig()},
            });
        }
        for (const RouteState& r : routes_)
            routeList.push_back({{"universe", r.route.universe}, {"interface", r.route.interfaceId}});
    }
    return {
        {"formatVersion", kConfigVersion},
        {"outputEnabled", outputEnabled()},
        {"outputRateHz", outputRate()},
        {"programmerMode", programmerModeName(programmerMode())},
        {"sourceTimeoutMs", store_.sourceTimeout().count()},
        {"holdLastLook", store_.holdLastLook()},
        {"interfaces", interfaceList},
        {"routes", routeList},
    };
}

bool DmxManager::loadConfig(const nlohmann::json& config, std::string& error) {
    // Build everything first, so an invalid file leaves the current setup untouched.
    std::vector<std::unique_ptr<DmxInterface>> created;
    std::vector<OutputRoute> newRoutes;
    ProgrammerMode mode = ProgrammerMode::Merge;
    double rate = kDefaultOutputRate;
    bool outputOn = true;
    long long timeoutMs = kDefaultSourceTimeout.count();
    bool hold = true;
    try {
        if (!config.is_object()) {
            error = "DMX configuration must be a JSON object";
            return false;
        }
        const int version = config.value("formatVersion", kConfigVersion);
        if (version > kConfigVersion) {
            error = std::format("DMX configuration version {} is newer than supported ({})", version, kConfigVersion);
            return false;
        }

        std::set<InterfaceId> ids;
        for (const nlohmann::json& entry : config.value("interfaces", nlohmann::json::array())) {
            const std::string type = entry.at("type").get<std::string>();
            const long long id = entry.at("id").get<long long>();
            if (id < 1 || id > 0xFFFFFFFFLL || !ids.insert(static_cast<InterfaceId>(id)).second) {
                error = std::format("invalid or duplicate interface id {}", id);
                return false;
            }
            std::unique_ptr<DmxInterface> iface = registry_.create(type);
            if (!iface) {
                // A newer or third-party type: keep loading the rest.
                log::warn("dmx", "skipping DMX interface {} of unknown type '{}'", id, type);
                continue;
            }
            iface->setLabel(entry.value("label", std::string()));
            iface->setEnabled(entry.value("enabled", false));
            iface->setInputEnabled(entry.value("inputEnabled", true));
            std::string interfaceError;
            if (!iface->loadConfig(entry.value("settings", nlohmann::json::object()), interfaceError)) {
                error = std::format("interface {} ({}): {}", id, type, interfaceError);
                return false;
            }
            iface->attach(&store_, static_cast<InterfaceId>(id));
            created.push_back(std::move(iface));
        }

        for (const nlohmann::json& entry : config.value("routes", nlohmann::json::array())) {
            const int universe = entry.at("universe").get<int>();
            const long long id = entry.at("interface").get<long long>();
            if (universe < 1 || universe > 0xFFFF) {
                error = std::format("route with invalid universe {}", universe);
                return false;
            }
            const bool known =
                std::any_of(created.begin(), created.end(), [&](const auto& iface) { return iface->id() == id; });
            if (known) newRoutes.push_back({static_cast<UniverseId>(universe), static_cast<InterfaceId>(id)});
        }

        const std::string modeName = config.value("programmerMode", std::string("merge"));
        if (modeName != "merge" && modeName != "override") {
            error = std::format("unknown programmer mode '{}'", modeName);
            return false;
        }
        mode = modeName == "override" ? ProgrammerMode::Override : ProgrammerMode::Merge;

        rate = config.value("outputRateHz", kDefaultOutputRate);
        outputOn = config.value("outputEnabled", true);
        timeoutMs = std::clamp<long long>(config.value("sourceTimeoutMs", timeoutMs), 100, 60000);
        hold = config.value("holdLastLook", true);
    } catch (const nlohmann::json::exception& e) {
        error = std::format("DMX configuration: {}", e.what());
        return false;
    }

    // Swap in the new setup, then stop the old interfaces outside the lock.
    std::vector<std::unique_ptr<DmxInterface>> old;
    {
        std::lock_guard lock(mutex_);
        old.swap(interfaces_);
        interfaces_ = std::move(created);
        nextId_ = 1;
        for (const auto& iface : interfaces_) nextId_ = std::max(nextId_, iface->id() + 1);
        routes_.clear();
        for (const OutputRoute& route : newRoutes) routes_.push_back(RouteState{route});
    }
    for (const auto& iface : old) iface->stop();
    old.clear();

    setOutputRate(rate);
    setOutputEnabled(outputOn);
    store_.setSourceTimeout(std::chrono::milliseconds(timeoutMs));
    store_.setHoldLastLook(hold);
    store_.setProgrammerMode(mode);
    return true;
}

}  // namespace dmxviz::dmx
