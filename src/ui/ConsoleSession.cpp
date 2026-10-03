#include "ui/ConsoleSession.h"

#include "ui/PatchModel.h"

#include <vector>

namespace dmxviz::ui {

ConsoleFixture* ConsoleSession::fixture(const stage::Scene& scene, const fixtures::FixtureLibrary& library,
                                        NodeId id) {
    const stage::Node* node = scene.find(id);
    const stage::FixtureContent* content = node != nullptr ? node->as<stage::FixtureContent>() : nullptr;
    if (content == nullptr) return nullptr;
    const fixtures::FixtureType* type = library.find(content->fixtureTypeId);
    const fixtures::DmxMode* mode = type != nullptr ? fixtureMode(*type, *content) : nullptr;
    if (mode == nullptr) return nullptr;

    const auto it = fixtures_.find(id);
    if (it != fixtures_.end()) {
        ConsoleFixture& existing = *it->second;
        if (&existing.type() == type && &existing.mode() == mode) {
            existing.repatch(content->patch, model_);
            return &existing;
        }
        existing.release(model_);  // another type or mode: start over
        fixtures_.erase(it);
    }
    auto created = std::make_unique<ConsoleFixture>(id, *type, *mode, content->patch);
    ConsoleFixture* result = created.get();
    fixtures_.emplace(id, std::move(created));
    return result;
}

void ConsoleSession::prune(const stage::Scene& scene) {
    std::vector<NodeId> gone;
    for (const auto& entry : fixtures_)
        if (!scene.contains(entry.first)) gone.push_back(entry.first);
    for (const NodeId id : gone) {
        fixtures_[id]->release(model_);
        fixtures_.erase(id);
    }
}

void ConsoleSession::clearAll() {
    model_.releaseAll();
    fixtures_.clear();
}

int ConsoleSession::capturedCount() const {
    int count = 0;
    for (const auto& entry : fixtures_)
        if (entry.second->captured()) ++count;
    return count;
}

}  // namespace dmxviz::ui
