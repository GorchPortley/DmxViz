#include "app/DemoShow.h"

#include "core/Log.h"
#include "fixtures/AttributeEncoder.h"
#include "stage/NodeFactory.h"
#include "stage/PlacementTools.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <string>
#include <vector>

namespace dmxviz::app {

namespace {

using stage::Transform;
namespace factory = stage::factory;

constexpr const char* kProfileSpotId = "generic/profile-spot";
constexpr int kSpotsPerTruss = 4;
constexpr int kExtrasPerType = 4;

constexpr float kDeckHeight = 0.6f;      // top of the stage deck
constexpr float kTrussHeight = 5.5f;     // centre of the truss
constexpr float kTrussLength = 12.0f;
constexpr float kUpstageTrussZ = -2.0f;
constexpr float kDownstageTrussZ = 2.2f;
constexpr float kMidTrussZ = 0.1f;

Transform at(const glm::vec3& position) {
    Transform t;
    t.position = position;
    return t;
}

// Hands out consecutive DMX addresses, moving on to the next universe when a footprint does not fit.
class Patcher {
public:
    stage::DmxPatch next(int footprint) {
        if (address_ + footprint - 1 > static_cast<int>(dmx::kUniverseSize)) {
            ++universe_;
            address_ = 1;
        }
        const stage::DmxPatch patch{static_cast<std::uint32_t>(universe_), static_cast<std::uint32_t>(address_)};
        address_ += footprint;
        return patch;
    }

private:
    int universe_ = 1;
    int address_ = 1;
};

// Adds one fixture (first DMX mode, next free address) and clamps it under the truss at position x.
void hangFixture(stage::Scene& scene, Patcher& patcher, const fixtures::FixtureType& type, float x, NodeId truss,
                 float trussZ, int& fixtureNumber) {
    const fixtures::DmxMode& mode = type.modes.front();
    ++fixtureNumber;
    stage::NodeData data = factory::fixture(type.id, mode.name, patcher.next(mode.footprint), fixtureNumber);
    data.name = std::format("{} {}", type.name, fixtureNumber);
    const NodeId id = scene.addNode(std::move(data));
    const auto hang = stage::tools::hangOnTruss(scene, id, truss, glm::vec3(x, kTrussHeight, trussZ));
    if (hang) scene.setTransform(id, hang->local);
}

// x position of item `index` of `count` spread evenly along the truss.
float trussSlot(int index, int count) {
    return (static_cast<float>(index) + 0.5f) / static_cast<float>(count) * kTrussLength - kTrussLength * 0.5f;
}

}  // namespace

void buildDemoShow(stage::Scene& scene, const fixtures::FixtureLibrary& library, render::Environment& environment) {
    scene.clear();

    // Set: floor, deck, back wall and a person for scale.
    scene.addNode(factory::floor(30.0f, 20.0f));
    stage::NodeData deck = factory::stageDeck(6, 6, kDeckHeight);
    deck.name = "Stage deck";
    scene.addNode(std::move(deck));
    stage::NodeData wall = factory::wall(12.0f, 7.0f, 0.15f);
    wall.name = "Back wall";
    scene.addNode(std::move(wall), at({0.0f, 0.0f, -3.2f}));
    scene.addNode(factory::referenceFigure(), at({1.5f, kDeckHeight, 1.0f}));

    // Truss lines.
    const stage::TrussProfile* f34 = stage::findTrussProfile("F34");
    const stage::TrussProfile profile = f34 ? *f34 : stage::TrussProfile{};
    auto addTruss = [&](const char* name, float z) {
        stage::NodeData data = factory::trussStraight(profile, kTrussLength);
        data.name = name;
        return scene.addNode(std::move(data), at({0.0f, kTrussHeight, z}));
    };
    const NodeId upstage = addTruss("Upstage truss", kUpstageTrussZ);
    const NodeId downstage = addTruss("Downstage truss", kDownstageTrussZ);

    // Fixtures.
    Patcher patcher;
    int fixtureNumber = 0;
    if (const fixtures::FixtureType* spot = library.find(kProfileSpotId)) {
        for (int i = 0; i < kSpotsPerTruss; ++i)
            hangFixture(scene, patcher, *spot, trussSlot(i, kSpotsPerTruss), upstage, kUpstageTrussZ, fixtureNumber);
        for (int i = 0; i < kSpotsPerTruss; ++i)
            hangFixture(scene, patcher, *spot, trussSlot(i, kSpotsPerTruss), downstage, kDownstageTrussZ, fixtureNumber);
    } else {
        log::warn("app", "demo show: fixture type {} is not in the library, the stage has no spots", kProfileSpotId);
    }

    // Every other generic fixture type found in the library goes on a third truss.
    std::vector<const fixtures::FixtureType*> extras;
    for (const fixtures::FixtureType* type : library.all())
        if (type->id.starts_with("generic/") && type->id != kProfileSpotId && !type->modes.empty()) extras.push_back(type);
    if (!extras.empty()) {
        const NodeId mid = addTruss("Mid truss", kMidTrussZ);
        // All extras spread evenly along the truss, grouped by type.
        const int total = kExtrasPerType * static_cast<int>(extras.size());
        int placed = 0;
        for (const fixtures::FixtureType* type : extras)
            for (int i = 0; i < kExtrasPerType; ++i, ++placed)
                hangFixture(scene, patcher, *type, trussSlot(placed, total), mid, kMidTrussZ, fixtureNumber);
    }

    environment = render::Environment{};
    environment.hazeDensity = 0.4f;
    environment.hazeVariation = 0.35f;
    environment.ambient = glm::vec3(0.05f);  // enough to see the set and the truss between the beams
    log::info("app", "demo show: {} nodes, {} fixtures", scene.nodeCount(), fixtureNumber);
}

// ---------------------------------------------------------------------------
// Test pattern

namespace {

// Linear RGB colours the pattern cycles through.
const glm::vec3 kPalette[] = {
    {1.00f, 1.00f, 1.00f},  // white
    {1.00f, 0.05f, 0.05f},  // red
    {0.10f, 0.30f, 1.00f},  // blue
    {1.00f, 0.45f, 0.05f},  // amber
    {0.10f, 1.00f, 0.25f},  // green
    {1.00f, 0.10f, 0.80f},  // magenta
    {0.10f, 0.90f, 1.00f},  // cyan
    {1.00f, 0.75f, 0.45f},  // warm white
};

// Number of slots of the wheel behind a wheel attribute (0 if the mode has no such wheel).
int wheelSlotCount(const fixtures::AttributeEncoder& encoder, fixtures::Attribute attribute) {
    for (const auto& control : encoder.controls())
        if (control.attribute == attribute && control.wheel != nullptr) return static_cast<int>(control.wheel->slots.size());
    return 0;
}

// Pan and tilt (radians) that make a hanging fixture at `from` shine at `target`.
// Convention: pan = 0 / tilt = 0 points straight down, pan turns about the vertical axis, tilt leans
// the head away from vertical. The solution with the smaller pan is used (head flipped over if needed).
void aimAngles(const glm::vec3& from, const glm::vec3& target, float& pan, float& tilt) {
    const glm::vec3 d = glm::normalize(target - from);
    const float horizontal = std::sqrt(d.x * d.x + d.z * d.z);
    tilt = std::atan2(horizontal, -d.y);
    pan = horizontal > 1e-4f ? std::atan2(-d.x, -d.z) : 0.0f;
    const float flippedPan = pan > 0.0f ? pan - kPi : pan + kPi;
    if (std::abs(flippedPan) < std::abs(pan)) {
        pan = flippedPan;
        tilt = -tilt;
    }
}

}  // namespace

void applyTestPattern(const stage::Scene& scene, const fixtures::FixtureLibrary& library, dmx::DmxManager& dmx) {
    using fixtures::Attribute;
    std::map<dmx::UniverseId, dmx::UniverseData> universes;
    int index = 0;

    for (NodeId id : scene.nodesOfKind(stage::NodeKind::Fixture)) {
        const stage::Node* node = scene.find(id);
        const stage::FixtureContent* fixture = node ? node->as<stage::FixtureContent>() : nullptr;
        if (fixture == nullptr || !fixture->patch.patched() || fixture->patch.universe > 0xFFFFu) continue;
        const fixtures::FixtureType* type = library.find(fixture->fixtureTypeId);
        if (type == nullptr || type->modes.empty()) continue;
        const fixtures::DmxMode* mode = type->findMode(fixture->modeName);
        if (mode == nullptr) mode = &type->modes.front();

        const fixtures::AttributeEncoder encoder(*type, *mode);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(mode->footprint), 0);
        encoder.writeDefaults(bytes);

        // On: full dimmer, open shutter, a colour and a slightly different zoom per fixture.
        encoder.setPhysical(bytes, Attribute::Dimmer, 1.0f);
        encoder.setShutter(bytes, fixtures::FunctionKind::ShutterOpen);
        encoder.setColor(bytes, kPalette[index % std::size(kPalette)]);
        encoder.setNormalized(bytes, Attribute::Zoom, 0.25f + 0.15f * static_cast<float>(index % 4));
        encoder.setNormalized(bytes, Attribute::Focus1, 1.0f);

        // Gobos: alternate between the two wheels, skipping slot 1 (open).
        const int gobos1 = wheelSlotCount(encoder, Attribute::Gobo1);
        const int gobos2 = wheelSlotCount(encoder, Attribute::Gobo2);
        const bool useSecond = gobos2 > 1 && (index % 2 == 1 || gobos1 <= 1);
        if (useSecond)
            encoder.setWheelSlot(bytes, Attribute::Gobo2, static_cast<float>(2 + (index / 2) % (gobos2 - 1)));
        else if (gobos1 > 1)
            encoder.setWheelSlot(bytes, Attribute::Gobo1, static_cast<float>(2 + (index / 2) % (gobos1 - 1)));

        // Every fourth fixture also gets its prism (the last slot of the prism wheel).
        const int prisms = wheelSlotCount(encoder, Attribute::Prism1);
        if (prisms > 1 && index % 4 == 3) encoder.setWheelSlot(bytes, Attribute::Prism1, static_cast<float>(prisms));

        // Pan/tilt: fixtures aim at points spread across the deck, crossing from the other side of the stage.
        if (encoder.has(Attribute::Pan) && encoder.has(Attribute::Tilt)) {
            const glm::vec3 position = glm::vec3(scene.worldMatrix(id)[3]);
            const float side = position.z < 1.0f ? 1.0f : -1.0f;  // upstage fixtures shine toward the audience
            const glm::vec3 target(-0.55f * position.x, kDeckHeight, 0.4f + side * 1.3f);
            float pan = 0.0f;
            float tilt = 0.0f;
            aimAngles(position, target, pan, tilt);
            encoder.setPhysical(bytes, Attribute::Pan, pan);
            encoder.setPhysical(bytes, Attribute::Tilt, tilt);
        }

        dmx::UniverseData& data = universes[static_cast<dmx::UniverseId>(fixture->patch.universe)];
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            const std::size_t slot = fixture->patch.address - 1 + i;
            if (slot < data.size()) data[slot] = bytes[i];
        }
        ++index;
    }

    for (const auto& [universe, data] : universes) dmx.setProgrammerUniverse(universe, data);
    dmx.setProgrammerMode(dmx::ProgrammerMode::Override);
    log::info("app", "test pattern: driving {} fixtures on {} universe(s)", index, universes.size());
}

}  // namespace dmxviz::app
