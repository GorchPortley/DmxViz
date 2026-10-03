#include "RigBuilder.h"

#include "dmx/DmxTypes.h"
#include "stage/NodeFactory.h"
#include "stage/TrussProfile.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace simbench {

namespace {

using namespace dmxviz;
namespace factory = stage::factory;

// Share of each bundled fixture type in a 500 fixture rig. Pixel bars carry most of the beams
// (8 pixel beams each), the movers carry the gobos, prisms and the most DMX channels.
struct TypeShare {
    const char* typeId;
    const char* modeName;  // "" = first mode
    int per500;
};
constexpr TypeShare kShares[] = {
    {"generic/profile-spot", "Standard", 110}, {"generic/beam-mover", "Standard", 70},
    {"generic/led-wash-mover", "Standard", 80}, {"generic/rgbw-par", "Extended", 50},
    {"generic/rgb-par", "Extended", 40},       {"generic/led-strobe", "Strobe", 30},
    {"generic/par64-dimmer", "Dimmer", 20},    {"generic/pixel-bar", "Pixel", 100},
};

constexpr int kTrussLines = 10;
constexpr float kTrussLength = 24.0f;
constexpr float kTrussHeight = 6.0f;

// Hands out DMX addresses spread round-robin over `universes` universes, so every universe carries data.
class SpreadPatcher {
public:
    explicit SpreadPatcher(int universes) : next_(static_cast<std::size_t>(std::max(universes, 1)), 1) {}

    stage::DmxPatch next(int footprint) {
        for (std::size_t tries = 0; tries < next_.size(); ++tries) {
            const std::size_t u = cursor_++ % next_.size();
            if (next_[u] + footprint - 1 <= static_cast<int>(dmx::kUniverseSize)) {
                const stage::DmxPatch patch{static_cast<std::uint32_t>(u + 1), static_cast<std::uint32_t>(next_[u])};
                next_[u] += footprint;
                return patch;
            }
        }
        return {};  // rig does not fit: the fixture stays unpatched
    }

private:
    std::vector<int> next_;
    std::size_t cursor_ = 0;
};

stage::Transform at(const glm::vec3& position) {
    stage::Transform t;
    t.position = position;
    return t;
}

}  // namespace

bool buildRig(stage::Scene& scene, const fixtures::FixtureLibrary& library, const RigOptions& options,
              std::vector<RigFixture>& out, std::string& error) {
    scene.clear();
    out.clear();

    // Scenery: a floor, a deck and a wall are the static meshes the renderer draws besides the fixtures.
    scene.addNode(factory::floor(40.0f, 30.0f));
    scene.addNode(factory::stageDeck(8, 6, 0.6f));
    scene.addNode(factory::wall(24.0f, 8.0f, 0.15f), at({0.0f, 0.0f, -8.0f}));

    const stage::TrussProfile* f34 = stage::findTrussProfile("F34");
    const stage::TrussProfile profile = f34 ? *f34 : stage::TrussProfile{};
    std::vector<NodeId> trusses;
    for (int i = 0; i < kTrussLines; ++i) {
        stage::NodeData data = factory::trussStraight(profile, kTrussLength);
        data.name = std::format("Truss {}", i + 1);
        const float z = -6.0f + 1.4f * static_cast<float>(i);
        trusses.push_back(scene.addNode(std::move(data), at({0.0f, kTrussHeight, z})));
    }

    // Scale the share table to the requested fixture count; the remainder goes to the first type.
    std::vector<int> counts;
    int total = 0;
    for (const TypeShare& s : kShares) {
        counts.push_back(options.fixtures * s.per500 / 500);
        total += counts.back();
    }
    counts[0] += std::max(options.fixtures - total, 0);

    SpreadPatcher patcher(options.universes);
    int number = 0;
    for (std::size_t t = 0; t < std::size(kShares); ++t) {
        const fixtures::FixtureType* type = library.find(kShares[t].typeId);
        if (type == nullptr || type->modes.empty()) {
            error = std::format("fixture type '{}' is not in the library", kShares[t].typeId);
            return false;
        }
        const fixtures::DmxMode* mode = type->findMode(kShares[t].modeName);
        if (mode == nullptr) mode = &type->modes.front();

        for (int i = 0; i < counts[t]; ++i, ++number) {
            const stage::DmxPatch patch = patcher.next(mode->footprint);
            stage::NodeData data = factory::fixture(type->id, mode->name, patch, number + 1);
            data.name = std::format("{} {}", type->name, number + 1);
            // Fixtures are children of the truss lines like in a real show (world matrices come from the parent).
            const std::size_t line = static_cast<std::size_t>(number) % trusses.size();
            const int slot = number / static_cast<int>(trusses.size());
            const int perLine = (options.fixtures + kTrussLines - 1) / kTrussLines;
            const float x = (static_cast<float>(slot) + 0.5f) / static_cast<float>(perLine) * (kTrussLength - 1.0f) -
                            0.5f * (kTrussLength - 1.0f);
            const NodeId id = scene.addNode(std::move(data), at({x, -0.35f, 0.0f}), trusses[line]);
            out.push_back({id, type, mode, patch});
        }
    }
    return true;
}

}  // namespace simbench
