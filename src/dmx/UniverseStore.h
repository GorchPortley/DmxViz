#pragma once
// UniverseStore: thread-safe home of all incoming DMX, merged per universe.
//
// It keeps the latest frame of every *source* (one sender of one universe on one
// interface). IO threads call submit()/removeSource() for each packet; the main thread
// calls snapshot() once per frame; the DmxManager output thread calls mergeForOutput().
// A single mutex guards everything and every critical section is short (a 512-byte copy
// or one merge), so IO threads never wait long.
//
// Merge rule, per channel: among the sources providing the channel, the highest
// priority wins; equal priorities merge HTP (highest value wins).
// Timeouts: a source silent for longer than the timeout (default 2.5 s, E1.31 "network
// data loss") stops contributing. Exception ("hold last look", like most DMX nodes): if
// *all* network sources of a universe timed out, they are kept as "held" sources so the
// stage keeps its last look; the first fresh packet for that universe discards them.
// Sources that leave on purpose (sACN stream-terminated, interface stopped, programmer
// cleared) release their channels immediately.
//
// The local programmer (test console) is just another source. It only provides the
// channels it has touched: at priority 100 in Merge mode (HTP with inputs) or 201 in
// Override mode (beats every network source).
//
// Every function that depends on time takes `now`, so tests can drive the clock.

#include "dmx/DmxSnapshot.h"
#include "dmx/DmxTypes.h"
#include "dmx/NetAddress.h"
#include "dmx/RateMeter.h"

#include <array>
#include <bitset>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dmxviz::dmx {

// Identifies one sender: the interface it arrived on plus a protocol-specific sender id
// (sACN CID; Art-Net sender IP and port; all zero for USB widgets).
struct SourceId {
    InterfaceId interfaceId = 0;
    std::array<std::uint8_t, 16> sender{};

    static SourceId fromEndpoint(InterfaceId interfaceId, const Endpoint& endpoint);
    static SourceId fromCid(InterfaceId interfaceId, const std::array<std::uint8_t, 16>& cid);
    bool operator==(const SourceId&) const = default;
};

// Who sent a frame. `name` is only copied when it differs from the stored one, so
// repeated packets do not allocate.
struct SourceDescriptor {
    SourceId id;
    Protocol protocol = Protocol::ArtNet;
    std::uint8_t priority = kDefaultPriority;
    Endpoint endpoint;
    std::string_view name;
    bool preview = false;
};

class UniverseStore {
public:
    // ---- input (any thread) ---------------------------------------------------------
    // Stores the latest frame of a source. `slots` may be shorter than 512 (the rest is
    // treated as zero); extra slots are ignored. Universe 0 is ignored.
    void submit(UniverseId universe, const SourceDescriptor& source, std::span<const std::uint8_t> slots,
                TimePoint now = Clock::now());
    // The source leaves now (sACN stream-terminated).
    void removeSource(UniverseId universe, const SourceId& id);
    // Drops every source of an interface (it was stopped or removed).
    void removeInterfaceSources(InterfaceId interfaceId);
    // Forgets all sources and held values (the programmer included).
    void clear();

    // ---- programmer / test console (any thread) --------------------------------------
    void setProgrammerChannel(UniverseId universe, std::uint16_t address1Based, std::uint8_t value);
    void setProgrammerUniverse(UniverseId universe, std::span<const std::uint8_t> values);  // touches all 512
    void clearProgrammerUniverse(UniverseId universe);
    void clearProgrammer();
    void setProgrammerMode(ProgrammerMode mode);
    ProgrammerMode programmerMode() const;
    // The programmer's own values for a universe (untouched channels read 0), or nullopt.
    std::optional<UniverseData> programmerValues(UniverseId universe) const;

    // ---- output -----------------------------------------------------------------------
    // Fills `out` with every universe, merged at time `now`. Reuses `out`'s storage.
    void snapshot(DmxSnapshot& out, TimePoint now = Clock::now());
    // The merged universe as an output interface should send it: identical to the
    // snapshot except that sources received on `excludedInterface` are left out, so an
    // interface never echoes its own input back. Returns false (and zeros `out`) if no
    // data exists for the universe.
    bool mergeForOutput(UniverseId universe, InterfaceId excludedInterface, UniverseData& out,
                        TimePoint now = Clock::now());

    // ---- settings ---------------------------------------------------------------------
    void setSourceTimeout(std::chrono::milliseconds timeout);
    std::chrono::milliseconds sourceTimeout() const;
    void setHoldLastLook(bool hold);
    bool holdLastLook() const;

private:
    struct Source {
        SourceId id;
        Protocol protocol = Protocol::ArtNet;
        std::uint8_t priority = kDefaultPriority;
        Endpoint endpoint;
        std::string name;
        bool preview = false;
        UniverseData data{};
        // The programmer provides only touched channels; network sources provide all.
        bool partial = false;
        std::bitset<kUniverseSize> provided;
        bool persistent = false;  // never times out (programmer)
        bool held = false;        // timed out, kept only to hold the last look
        TimePoint lastSeen{};
        RateMeter rate;
    };

    struct Slot {
        std::vector<Source> sources;  // empty = universe unknown / released
        UniverseData merged{};
        bool dirty = false;  // merged needs recomputing
        TimePoint lastUpdate{};
    };

    static constexpr InterfaceId kExcludeNothing = 0xFFFFFFFFu;

    Source* findSource(Slot& slot, const SourceId& id);
    Source& programmerSource(UniverseId universe);
    void expireLocked(Slot& slot, TimePoint now);
    void mergeIfDirty(Slot& slot);
    static bool mergeSources(const std::vector<Source>& sources, InterfaceId excluded, UniverseData& out);
    std::uint8_t programmerPriority() const;

    mutable std::mutex mutex_;
    std::map<UniverseId, Slot> slots_;
    std::chrono::milliseconds timeout_ = kDefaultSourceTimeout;
    bool holdLastLook_ = true;
    ProgrammerMode programmerMode_ = ProgrammerMode::Merge;
};

}  // namespace dmxviz::dmx
