#pragma once
// ConsoleSession: every fixture the test console has touched, plus the programmer model
// behind them. Keeps the console state in step with the scene: a fixture that is deleted
// leaves the programmer, a re-patched one moves along, and a fixture whose type or mode
// changed starts over.

#include "core/Id.h"
#include "fixtures/FixtureLibrary.h"
#include "stage/Scene.h"
#include "ui/ConsoleFixture.h"
#include "ui/ProgrammerModel.h"

#include <memory>
#include <unordered_map>

namespace dmxviz::ui {

class ConsoleSession {
public:
    explicit ConsoleSession(dmx::DmxManager& manager) : model_(manager) {}

    ProgrammerModel& model() { return model_; }
    const ProgrammerModel& model() const { return model_; }

    // The console state of a fixture node, created on first use. Null when the node is not a
    // fixture or its type or mode is unknown.
    ConsoleFixture* fixture(const stage::Scene& scene, const fixtures::FixtureLibrary& library, NodeId id);

    // Releases and forgets fixtures whose node no longer exists.
    void prune(const stage::Scene& scene);
    // Empties the programmer completely and forgets all fixtures.
    void clearAll();

    // Fixtures currently held in the programmer.
    int capturedCount() const;

private:
    ProgrammerModel model_;
    std::unordered_map<NodeId, std::unique_ptr<ConsoleFixture>> fixtures_;
};

}  // namespace dmxviz::ui
