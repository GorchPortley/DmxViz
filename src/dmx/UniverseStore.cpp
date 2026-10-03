#include "dmx/UniverseStore.h"

#include "dmx/protocol/ByteIo.h"

#include <algorithm>

namespace dmxviz::dmx {
namespace {

const SourceId kProgrammerSourceId{};  // interface 0, zero sender

}  // namespace

SourceId SourceId::fromEndpoint(InterfaceId interfaceId, const Endpoint& endpoint) {
    SourceId id;
    id.interfaceId = interfaceId;
    bytes::writeU32BE(&id.sender[0], endpoint.address.value);
    bytes::writeU16BE(&id.sender[4], endpoint.port);
    return id;
}

SourceId SourceId::fromCid(InterfaceId interfaceId, const std::array<std::uint8_t, 16>& cid) {
    SourceId id;
    id.interfaceId = interfaceId;
    id.sender = cid;
    return id;
}

// ---- input ------------------------------------------------------------------------------

bool UniverseStore::submit(UniverseId universe, const SourceDescriptor& desc, std::span<const std::uint8_t> slots,
                           TimePoint now) {
    if (universe == kInvalidUniverse) return false;
    std::lock_guard lock(mutex_);
    Slot& slot = slots_[universe];  // allocates only the first time a universe appears

    // Fresh data ends "hold last look": drop the timed-out sources that were kept for it.
    eraseSourcesIf(slot, [](const Source& s) { return s.held; });

    Source* source = findSource(slot, desc.id);
    if (!source) {
        if (!makeRoomForSource(slot, now)) return false;
        source = &slot.sources.emplace_back();
        ++sourceCount_;
        source->id = desc.id;
    }
    source->protocol = desc.protocol;
    source->priority = desc.priority;
    source->endpoint = desc.endpoint;
    source->preview = desc.preview;
    if (source->name != desc.name) source->name.assign(desc.name.data(), desc.name.size());

    const std::size_t n = std::min(slots.size(), kUniverseSize);
    std::copy_n(slots.begin(), n, source->data.begin());
    std::fill(source->data.begin() + static_cast<std::ptrdiff_t>(n), source->data.end(), std::uint8_t{0});
    source->lastSeen = now;
    source->rate.add(now);

    slot.lastUpdate = now;
    slot.dirty = true;
    expireLocked(slot, now);  // other senders of this universe may have gone quiet
    return true;
}

// A new network source needs a free place under both limits. Quiet sources are expired first, then
// "held" ones (kept only to hold a last look) are given up; if that is not enough the frame is dropped.
bool UniverseStore::makeRoomForSource(Slot& slot, TimePoint now) {
    if (slot.sources.size() >= kMaxSourcesPerUniverse) {
        expireLocked(slot, now);
        eraseSourcesIf(slot, [](const Source& s) { return s.held; });
        if (slot.sources.size() >= kMaxSourcesPerUniverse) return false;
    }
    if (sourceCount_ < kMaxSources) return true;

    // At most one full sweep per second, so a flood cannot make every packet scan all universes.
    if (now - lastSweep_ >= std::chrono::seconds(1)) {
        lastSweep_ = now;
        for (auto& entry : slots_) {
            expireLocked(entry.second, now);
            if (&entry.second != &slot) eraseSourcesIf(entry.second, [](const Source& s) { return s.held; });
        }
    }
    return sourceCount_ < kMaxSources;
}

void UniverseStore::removeSource(UniverseId universe, const SourceId& id) {
    std::lock_guard lock(mutex_);
    const auto it = slots_.find(universe);
    if (it == slots_.end()) return;
    if (eraseSourcesIf(it->second, [&](const Source& s) { return s.id == id; }) > 0) it->second.dirty = true;
}

void UniverseStore::removeInterfaceSources(InterfaceId interfaceId) {
    std::lock_guard lock(mutex_);
    for (auto& entry : slots_) {
        Slot& slot = entry.second;
        if (eraseSourcesIf(slot, [&](const Source& s) { return s.id.interfaceId == interfaceId; }) > 0)
            slot.dirty = true;
    }
}

void UniverseStore::clear() {
    std::lock_guard lock(mutex_);
    slots_.clear();
    sourceCount_ = 0;
}

// ---- programmer -------------------------------------------------------------------------

std::uint8_t UniverseStore::programmerPriority() const {
    return programmerMode_ == ProgrammerMode::Override ? kOverridePriority : kDefaultPriority;
}

UniverseStore::Source& UniverseStore::programmerSource(UniverseId universe) {
    Slot& slot = slots_[universe];
    if (Source* existing = findSource(slot, kProgrammerSourceId)) return *existing;
    Source& source = slot.sources.emplace_back();
    ++sourceCount_;
    source.id = kProgrammerSourceId;
    source.protocol = Protocol::Programmer;
    source.priority = programmerPriority();
    source.name = "Programmer";
    source.partial = true;
    source.persistent = true;
    return source;
}

void UniverseStore::setProgrammerChannel(UniverseId universe, std::uint16_t address1Based, std::uint8_t value) {
    if (universe == kInvalidUniverse || address1Based < 1 || address1Based > kUniverseSize) return;
    const TimePoint now = Clock::now();
    std::lock_guard lock(mutex_);
    Source& source = programmerSource(universe);
    source.data[address1Based - 1u] = value;
    source.provided.set(address1Based - 1u);
    source.lastSeen = now;
    Slot& slot = slots_[universe];
    slot.lastUpdate = now;
    slot.dirty = true;
}

void UniverseStore::setProgrammerUniverse(UniverseId universe, std::span<const std::uint8_t> values) {
    if (universe == kInvalidUniverse) return;
    const TimePoint now = Clock::now();
    std::lock_guard lock(mutex_);
    Source& source = programmerSource(universe);
    const std::size_t n = std::min(values.size(), kUniverseSize);
    std::copy_n(values.begin(), n, source.data.begin());
    std::fill(source.data.begin() + static_cast<std::ptrdiff_t>(n), source.data.end(), std::uint8_t{0});
    source.provided.set();
    source.lastSeen = now;
    Slot& slot = slots_[universe];
    slot.lastUpdate = now;
    slot.dirty = true;
}

void UniverseStore::clearProgrammerUniverse(UniverseId universe) {
    removeSource(universe, kProgrammerSourceId);
}

void UniverseStore::clearProgrammer() {
    std::lock_guard lock(mutex_);
    for (auto& entry : slots_) {
        Slot& slot = entry.second;
        if (eraseSourcesIf(slot, [](const Source& s) { return s.id == kProgrammerSourceId; }) > 0) slot.dirty = true;
    }
}

void UniverseStore::setProgrammerMode(ProgrammerMode mode) {
    std::lock_guard lock(mutex_);
    programmerMode_ = mode;
    for (auto& entry : slots_) {
        Slot& slot = entry.second;
        if (Source* source = findSource(slot, kProgrammerSourceId)) {
            source->priority = programmerPriority();
            slot.dirty = true;
        }
    }
}

ProgrammerMode UniverseStore::programmerMode() const {
    std::lock_guard lock(mutex_);
    return programmerMode_;
}

std::optional<UniverseData> UniverseStore::programmerValues(UniverseId universe) const {
    std::lock_guard lock(mutex_);
    const auto it = slots_.find(universe);
    if (it == slots_.end()) return std::nullopt;
    for (const Source& source : it->second.sources)
        if (source.id == kProgrammerSourceId) return source.data;
    return std::nullopt;
}

// ---- output -----------------------------------------------------------------------------

void UniverseStore::snapshot(DmxSnapshot& out, TimePoint now) {
    out.time_ = now;
    out.ids_.clear();
    std::size_t count = 0;

    std::lock_guard lock(mutex_);
    for (auto& [universe, slot] : slots_) {
        expireLocked(slot, now);
        if (slot.sources.empty()) continue;  // released universes are not shown
        mergeIfDirty(slot);

        out.ids_.push_back(universe);
        if (out.frames_.size() <= count) out.frames_.emplace_back();
        if (out.infos_.size() <= count) out.infos_.emplace_back();
        out.frames_[count] = slot.merged;

        UniverseInfo& info = out.infos_[count];
        info.universe = universe;
        info.lastUpdate = slot.lastUpdate;
        info.held = false;
        info.sources.resize(slot.sources.size());
        for (std::size_t i = 0; i < slot.sources.size(); ++i) {
            const Source& source = slot.sources[i];
            SourceInfo& si = info.sources[i];
            si.name.assign(source.name);  // reuses the string's capacity
            si.endpoint = source.endpoint;
            si.protocol = source.protocol;
            si.priority = source.priority;
            si.interfaceId = source.id.interfaceId;
            si.packetsPerSecond = source.persistent || source.held ? 0.0f : source.rate.perSecond(now);
            si.preview = source.preview;
            si.held = source.held;
            si.lastSeen = source.lastSeen;
            info.held = info.held || source.held;
        }
        ++count;
    }
    out.frames_.resize(count);
    out.infos_.resize(count);
}

bool UniverseStore::mergeForOutput(UniverseId universe, InterfaceId excludedInterface, UniverseData& out,
                                   TimePoint now) {
    std::lock_guard lock(mutex_);
    const auto it = slots_.find(universe);
    if (it == slots_.end()) {
        out.fill(0);
        return false;
    }
    expireLocked(it->second, now);
    return mergeSources(it->second.sources, excludedInterface, out);
}

// ---- settings ---------------------------------------------------------------------------

void UniverseStore::setSourceTimeout(std::chrono::milliseconds timeout) {
    std::lock_guard lock(mutex_);
    timeout_ = timeout;
}

std::chrono::milliseconds UniverseStore::sourceTimeout() const {
    std::lock_guard lock(mutex_);
    return timeout_;
}

void UniverseStore::setHoldLastLook(bool hold) {
    std::lock_guard lock(mutex_);
    holdLastLook_ = hold;
    if (hold) return;
    // Turning hold off releases whatever is currently being held.
    for (auto& entry : slots_) {
        Slot& slot = entry.second;
        if (eraseSourcesIf(slot, [](const Source& s) { return s.held; }) > 0) slot.dirty = true;
    }
}

bool UniverseStore::holdLastLook() const {
    std::lock_guard lock(mutex_);
    return holdLastLook_;
}

// ---- internals (mutex held) -------------------------------------------------------------

UniverseStore::Source* UniverseStore::findSource(Slot& slot, const SourceId& id) {
    for (Source& source : slot.sources)
        if (source.id == id) return &source;
    return nullptr;
}

void UniverseStore::expireLocked(Slot& slot, TimePoint now) {
    // Only network sources time out; the programmer is persistent.
    auto timedOut = [&](const Source& s) { return !s.persistent && !s.held && now - s.lastSeen > timeout_; };
    const auto expired = std::count_if(slot.sources.begin(), slot.sources.end(), timedOut);
    if (expired == 0) return;

    const auto live = std::count_if(slot.sources.begin(), slot.sources.end(),
                                    [](const Source& s) { return !s.persistent && !s.held; });
    if (holdLastLook_ && expired == live) {
        // The last network sender went quiet: keep its frame so the stage holds its look.
        for (Source& s : slot.sources)
            if (timedOut(s)) s.held = true;
    } else {
        eraseSourcesIf(slot, timedOut);
    }
    slot.dirty = true;
}

void UniverseStore::mergeIfDirty(Slot& slot) {
    if (!slot.dirty) return;
    mergeSources(slot.sources, kExcludeNothing, slot.merged);
    slot.dirty = false;
}

bool UniverseStore::mergeSources(const std::vector<Source>& sources, InterfaceId excluded, UniverseData& out) {
    // Per channel: highest priority wins, equal priority -> highest value (HTP).
    std::array<int, kUniverseSize> bestPriority;
    bestPriority.fill(-1);
    out.fill(0);
    bool any = false;
    for (const Source& source : sources) {
        if (source.id.interfaceId == excluded) continue;
        any = true;
        const int priority = source.priority;
        for (std::size_t ch = 0; ch < kUniverseSize; ++ch) {
            if (source.partial && !source.provided[ch]) continue;
            const std::uint8_t value = source.data[ch];
            if (priority > bestPriority[ch]) {
                bestPriority[ch] = priority;
                out[ch] = value;
            } else if (priority == bestPriority[ch] && value > out[ch]) {
                out[ch] = value;
            }
        }
    }
    return any;
}

}  // namespace dmxviz::dmx
