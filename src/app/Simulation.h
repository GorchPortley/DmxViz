#pragma once
// Simulation: turns the stage scene plus the live DMX snapshot into the flat
// RenderScene the renderer draws (ARCHITECTURE.md "Frame loop", steps 2 and 3).
//
// It owns one FixtureRuntime per fixture node and keeps them in step with the
// scene: a runtime is created when a fixture appears, rebuilt when its fixture
// type or DMX mode changes, and dropped when the node disappears. Static stage
// geometry (decks, truss, walls) is cached and only rebuilt when the scene or
// the selection changes. Steady-state frames do not allocate: every vector is
// reused.
//
// Main thread only.

#include "assets/AssetLibrary.h"
#include "dmx/DmxSnapshot.h"
#include "fixtures/FixtureAssets.h"
#include "fixtures/FixtureLibrary.h"
#include "fixtures/FixtureRuntime.h"
#include "render/RenderScene.h"
#include "stage/Scene.h"
#include "stage/Selection.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dmxviz::app {

class Simulation {
public:
    // The libraries must outlive the simulation (runtimes point into them).
    Simulation(assets::AssetLibrary& assets, const fixtures::FixtureLibrary& library);

    // Selection used for the highlight of meshes. May be null.
    void setSelection(const stage::Selection* selection) { selection_ = selection; }

    // Replaces out.meshes and out.beams with this frame's content (lines and the
    // environment are left alone). dt is clamped to 0.1 s so a stalled frame
    // does not make fixtures jump.
    void update(const stage::Scene& scene, const dmx::DmxSnapshot& dmx, float dt, double timeSeconds,
                render::RenderScene& out);

    // Forget everything derived from the scene. Call after the Scene object was
    // replaced (new or loaded project): its revision counter starts over.
    void invalidate();

    int fixtureCount() const { return static_cast<int>(order_.size()); }

private:
    // One fixture node and its runtime.
    struct FixtureEntry {
        NodeId node = kInvalidNode;
        const fixtures::FixtureType* type = nullptr;  // type the runtime was built on
        std::string typeId;
        std::string modeName;
        std::unique_ptr<fixtures::FixtureRuntime> runtime;  // null when the type is unknown
        std::vector<std::uint8_t> defaults;                 // channel defaults, used while no DMX arrives
        std::vector<std::uint8_t> footprint;                // scratch: this frame's bytes
        bool snapToTargets = true;                          // first frame: do not swing in from the default pose
        std::uint64_t syncStamp = 0;
    };

    void syncFixtures(const stage::Scene& scene);
    void refreshEntry(FixtureEntry& entry, const stage::FixtureContent& content);
    const fixtures::FixtureAssets* assetsFor(const fixtures::FixtureType& type);
    void fillFootprint(FixtureEntry& entry, const stage::DmxPatch& patch, const dmx::DmxSnapshot& dmx) const;
    void rebuildStaticMeshes(const stage::Scene& scene);
    Highlight highlightOf(const stage::Scene& scene, NodeId id) const;
    // Logs `message` once per `key`.
    void warnOnce(const std::string& key, const std::string& message);

    assets::AssetLibrary& assets_;
    const fixtures::FixtureLibrary& library_;
    const stage::Selection* selection_ = nullptr;

    std::unordered_map<NodeId, FixtureEntry> entries_;  // node-based: entry addresses stay valid
    std::vector<FixtureEntry*> order_;                  // scene order, rebuilt by syncFixtures()
    std::map<const fixtures::FixtureType*, std::unique_ptr<fixtures::FixtureAssets>> typeAssets_;
    std::unordered_set<std::string> reported_;  // problems already logged
    std::uint64_t syncStamp_ = 0;

    // Change detection for the caches.
    const stage::Scene* syncedScene_ = nullptr;
    std::uint64_t syncedRevision_ = 0;
    std::size_t syncedLibrarySize_ = 0;
    std::uint64_t staticRevision_ = 0;
    std::uint64_t staticSelectionRevision_ = 0;
    bool staticValid_ = false;
    std::vector<MeshInstance> staticMeshes_;
};

}  // namespace dmxviz::app
