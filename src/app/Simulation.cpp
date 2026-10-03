#include "app/Simulation.h"

#include "core/Log.h"
#include "fixtures/AttributeEncoder.h"

#include <algorithm>

namespace dmxviz::app {

Simulation::Simulation(assets::AssetLibrary& assets, const fixtures::FixtureLibrary& library)
    : assets_(assets), library_(library) {}

void Simulation::invalidate() {
    entries_.clear();
    order_.clear();
    syncedScene_ = nullptr;
    syncedRevision_ = 0;
    staticValid_ = false;
    staticMeshes_.clear();
}

void Simulation::warnOnce(const std::string& key, const std::string& message) {
    if (reported_.insert(key).second) log::warn("sim", "{}", message);
}

void Simulation::update(const stage::Scene& scene, const dmx::DmxSnapshot& dmx, float dt, double timeSeconds,
                        render::RenderScene& out) {
    dt = std::clamp(dt, 0.0f, 0.1f);

    // Fixture list: only when the scene (or the set of known fixture types) changed.
    if (syncedScene_ != &scene || syncedRevision_ != scene.revision() || syncedLibraryRevision_ != library_.revision())
        syncFixtures(scene);

    out.meshes.clear();
    out.beams.clear();

    rebuildStaticMeshes(scene);
    out.meshes.insert(out.meshes.end(), staticMeshes_.begin(), staticMeshes_.end());

    for (FixtureEntry* entry : order_) {
        if (!entry->runtime) continue;
        const stage::Node* node = scene.find(entry->node);
        if (node == nullptr) continue;
        const stage::FixtureContent* content = node->as<stage::FixtureContent>();
        if (content == nullptr) continue;

        fillFootprint(*entry, content->patch, dmx);
        fixtures::FixtureRuntime& runtime = *entry->runtime;
        runtime.setDmx(entry->footprint);
        if (entry->snapToTargets) {
            runtime.snapToTargets();
            entry->snapToTargets = false;
        }
        runtime.update(dt, timeSeconds);

        if (!scene.effectiveVisible(entry->node)) continue;
        const std::size_t firstMesh = out.meshes.size();
        runtime.emit(scene.worldMatrix(entry->node), entry->node, out.meshes, out.beams);
        const Highlight highlight = highlightOf(scene, entry->node);
        if (highlight != Highlight::None)
            for (std::size_t i = firstMesh; i < out.meshes.size(); ++i) out.meshes[i].highlight = highlight;
    }
}

void Simulation::syncFixtures(const stage::Scene& scene) {
    syncedScene_ = &scene;
    syncedRevision_ = scene.revision();
    syncedLibraryRevision_ = library_.revision();
    ++syncStamp_;

    order_.clear();
    for (NodeId id : scene.nodesOfKind(stage::NodeKind::Fixture)) {
        const stage::Node* node = scene.find(id);
        const stage::FixtureContent* content = node ? node->as<stage::FixtureContent>() : nullptr;
        if (content == nullptr) continue;
        FixtureEntry& entry = entries_[id];
        entry.node = id;
        entry.syncStamp = syncStamp_;
        refreshEntry(entry, *content);
        order_.push_back(&entry);
    }
    // Fixtures that left the scene.
    std::erase_if(entries_, [&](const auto& kv) { return kv.second.syncStamp != syncStamp_; });
}

void Simulation::refreshEntry(FixtureEntry& entry, const stage::FixtureContent& content) {
    const fixtures::FixtureType* type = library_.find(content.fixtureTypeId);
    const bool unchanged = entry.type == type && entry.typeId == content.fixtureTypeId &&
                           entry.modeName == content.modeName && (entry.runtime != nullptr) == (type != nullptr);
    if (!unchanged) {
        entry.runtime.reset();
        entry.type = type;
        entry.typeId = content.fixtureTypeId;
        entry.modeName = content.modeName;

        if (type == nullptr) {
            warnOnce("type:" + content.fixtureTypeId,
                     "unknown fixture type '" + content.fixtureTypeId + "', fixtures of this type are not drawn");
        } else if (type->modes.empty()) {
            warnOnce("modes:" + content.fixtureTypeId, "fixture type '" + content.fixtureTypeId + "' has no DMX modes");
        } else {
            const fixtures::DmxMode* mode = type->findMode(content.modeName);
            if (mode == nullptr) {
                warnOnce("mode:" + content.fixtureTypeId + "/" + content.modeName,
                         "fixture type '" + content.fixtureTypeId + "' has no mode '" + content.modeName +
                             "', using '" + type->modes.front().name + "'");
                mode = &type->modes.front();
            }
            entry.runtime = std::make_unique<fixtures::FixtureRuntime>(*type, *mode, assetsFor(*type));
            const fixtures::AttributeEncoder encoder(*type, *mode);
            entry.defaults.assign(static_cast<std::size_t>(entry.runtime->footprint()), 0);
            encoder.writeDefaults(entry.defaults);
            entry.footprint.assign(entry.defaults.size(), 0);
            entry.snapToTargets = true;
        }
    }
    if (entry.runtime) {
        fixtures::OrientationOptions orientation;
        orientation.invertPan = content.invertPan;
        orientation.invertTilt = content.invertTilt;
        orientation.panOffset = degToRad(content.panOffsetDeg);
        orientation.tiltOffset = degToRad(content.tiltOffsetDeg);
        entry.runtime->setOrientation(orientation);
    }
}

const fixtures::FixtureAssets* Simulation::assetsFor(const fixtures::FixtureType& type) {
    auto it = typeAssets_.find(&type);
    if (it == typeAssets_.end()) {
        std::vector<std::string> warnings;
        auto realized =
            std::make_unique<fixtures::FixtureAssets>(fixtures::FixtureAssets::realize(type, assets_, &warnings));
        for (const std::string& w : warnings) log::warn("sim", "{}: {}", type.id, w);
        it = typeAssets_.emplace(&type, std::move(realized)).first;
    }
    return it->second.get();
}

void Simulation::fillFootprint(FixtureEntry& entry, const stage::DmxPatch& patch, const dmx::DmxSnapshot& dmx) const {
    const dmx::UniverseData* universe = nullptr;
    if (patch.patched() && patch.universe <= 0xFFFFu)
        universe = dmx.universe(static_cast<dmx::UniverseId>(patch.universe));
    if (universe == nullptr) {
        // Nothing patched or no data yet: park the fixture in its default state.
        entry.footprint = entry.defaults;
        return;
    }
    const std::size_t first = patch.address - 1;
    for (std::size_t i = 0; i < entry.footprint.size(); ++i) {
        const std::size_t slot = first + i;
        entry.footprint[i] = slot < dmx::kUniverseSize ? (*universe)[slot] : std::uint8_t{0};
    }
}

void Simulation::rebuildStaticMeshes(const stage::Scene& scene) {
    const std::uint64_t selectionRevision = selection_ ? selection_->revision() : 0;
    if (staticValid_ && staticRevision_ == scene.revision() && staticSelectionRevision_ == selectionRevision &&
        syncedScene_ == &scene)
        return;
    staticMeshes_.clear();
    scene.collectRenderables(assets_, staticMeshes_, selection_);
    staticRevision_ = scene.revision();
    staticSelectionRevision_ = selectionRevision;
    staticValid_ = true;
}

Highlight Simulation::highlightOf(const stage::Scene& scene, NodeId id) const {
    if (selection_ == nullptr) return Highlight::None;
    // A selected or hovered group highlights everything below it.
    Highlight result = Highlight::None;
    for (NodeId n = id; n != kInvalidNode;) {
        result = std::max(result, selection_->highlightFor(n));
        const stage::Node* node = scene.find(n);
        n = node ? node->parent() : kInvalidNode;
    }
    return result;
}

}  // namespace dmxviz::app
