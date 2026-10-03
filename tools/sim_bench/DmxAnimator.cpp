#include "DmxAnimator.h"

#include <algorithm>
#include <cmath>

namespace simbench {

namespace {

using namespace dmxviz;
using fixtures::Attribute;

glm::vec3 hueToRgb(float hue) {
    const float h = hue - std::floor(hue);
    return glm::clamp(glm::vec3(std::abs(h * 6.0f - 3.0f) - 1.0f, 2.0f - std::abs(h * 6.0f - 2.0f),
                                2.0f - std::abs(h * 6.0f - 4.0f)),
                      0.0f, 1.0f);
}

int wheelSlots(const fixtures::AttributeEncoder& encoder, Attribute attribute) {
    for (const auto& control : encoder.controls())
        if (control.attribute == attribute && control.wheel != nullptr)
            return static_cast<int>(control.wheel->slots.size());
    return 0;
}

}  // namespace

DmxAnimator::DmxAnimator(const std::vector<RigFixture>& fixtures, int universes)
    : universes_(static_cast<std::size_t>(std::max(universes, 1))) {
    for (auto& u : universes_) u.fill(0);

    looks_.reserve(fixtures.size());
    for (std::size_t i = 0; i < fixtures.size(); ++i) {
        Look look;
        look.fixture = fixtures[i];
        look.encoder = std::make_unique<fixtures::AttributeEncoder>(*look.fixture.type, *look.fixture.mode);
        const fixtures::AttributeEncoder& e = *look.encoder;
        look.bytes.assign(static_cast<std::size_t>(look.fixture.mode->footprint), 0);
        look.phase = static_cast<float>(i) * 0.37f;
        look.pan = e.has(Attribute::Pan);
        look.tilt = e.has(Attribute::Tilt);
        look.zoom = e.has(Attribute::Zoom);
        look.dimmer = e.has(Attribute::Dimmer);
        look.gobo1Rotate = e.has(Attribute::Gobo1PosRotate);
        look.gobo1Slots = wheelSlots(e, Attribute::Gobo1);
        look.gobo2Slots = wheelSlots(e, Attribute::Gobo2);
        look.prismSlots = wheelSlots(e, Attribute::Prism1);

        // Every distinct geometry that has a colour channel gets its own colour (pixel bars: one per pixel).
        for (const auto& c : e.controls()) {
            const bool colour = c.attribute == Attribute::ColorAdd_R || c.attribute == Attribute::ColorAdd_C;
            if (colour && std::ranges::find(look.colorGeometries, c.geometry) == look.colorGeometries.end())
                look.colorGeometries.push_back(c.geometry);
        }
        looks_.push_back(std::move(look));
    }
}

void DmxAnimator::animateOne(Look& look, int index, double t) const {
    const fixtures::AttributeEncoder& e = *look.encoder;
    const auto time = static_cast<float>(t);
    const float p = look.phase;
    std::span<std::uint8_t> bytes(look.bytes);

    e.writeDefaults(bytes);
    if (look.dimmer) e.setPhysical(bytes, Attribute::Dimmer, 0.7f + 0.3f * std::sin(time * 1.3f + p));

    // Strobe burst: one second out of every four, at a rate that differs per fixture.
    const bool bursting = std::fmod(time + p, 4.0f) < 1.0f;
    if (bursting)
        e.setShutter(bytes, fixtures::FunctionKind::Strobe, 4.0f + static_cast<float>(index % 12));
    else
        e.setShutter(bytes, fixtures::FunctionKind::ShutterOpen);

    // Colour fade; every pixel of a pixel bar is a step further along the colour wheel.
    for (std::size_t g = 0; g < look.colorGeometries.size(); ++g)
        e.setColor(bytes, hueToRgb(time * 0.2f + p + 0.12f * static_cast<float>(g)), look.colorGeometries[g]);

    if (look.pan) e.setPhysical(bytes, Attribute::Pan, 1.2f * std::sin(time * 0.7f + p));
    if (look.tilt) e.setPhysical(bytes, Attribute::Tilt, 0.6f + 0.5f * std::sin(time * 0.9f + 1.3f * p));
    if (look.zoom) e.setNormalized(bytes, Attribute::Zoom, 0.5f + 0.5f * std::sin(time * 0.5f + p));

    // Gobo wheels: a new slot every ~0.7 s; the second wheel and the prism change at other rates.
    const int step = static_cast<int>(time * 1.5f) + index;
    if (look.gobo1Slots > 1) e.setWheelSlot(bytes, Attribute::Gobo1, static_cast<float>(1 + step % look.gobo1Slots));
    if (look.gobo2Slots > 1)
        e.setWheelSlot(bytes, Attribute::Gobo2, static_cast<float>(1 + (step / 2) % look.gobo2Slots));
    if (look.gobo1Rotate) e.setNormalized(bytes, Attribute::Gobo1PosRotate, 0.5f + 0.5f * std::sin(time + p));
    if (look.prismSlots > 1 && (static_cast<int>(time * 0.5f) + index) % 3 == 0)
        e.setWheelSlot(bytes, Attribute::Prism1, static_cast<float>(look.prismSlots));
}

void DmxAnimator::animate(double timeSeconds, dmx::UniverseStore& store) {
    for (std::size_t i = 0; i < looks_.size(); ++i) {
        Look& look = looks_[i];
        animateOne(look, static_cast<int>(i), timeSeconds);
        const stage::DmxPatch& patch = look.fixture.patch;
        if (!patch.patched() || patch.universe > universes_.size()) continue;
        dmx::UniverseData& data = universes_[patch.universe - 1];
        for (std::size_t b = 0; b < look.bytes.size(); ++b) {
            const std::size_t slot = patch.address - 1 + b;
            if (slot < data.size()) data[slot] = look.bytes[b];
        }
    }
    for (std::size_t u = 0; u < universes_.size(); ++u)
        store.setProgrammerUniverse(static_cast<dmx::UniverseId>(u + 1), universes_[u]);
}

}  // namespace simbench
