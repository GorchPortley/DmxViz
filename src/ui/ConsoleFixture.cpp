#include "ui/ConsoleFixture.h"

#include "fixtures/ColorMath.h"

#include <algorithm>

namespace dmxviz::ui {

using fixtures::Attribute;

ConsoleFixture::ConsoleFixture(NodeId node, const fixtures::FixtureType& type, const fixtures::DmxMode& mode,
                               stage::DmxPatch patch)
    : node_(node), type_(type), mode_(mode), encoder_(type, mode), patch_(patch) {
    bytes_.assign(static_cast<std::size_t>(std::max(mode.footprint, 0)), 0);
    resetToDefaultLook();  // so the UI shows the starting values before anything was touched
}

// ---------------------------------------------------------------------------
// Default look

// Where the channel's default DMX value lies within the first function of `attribute` (0..1),
// the inverse of AttributeEncoder::setNormalized().
float ConsoleFixture::defaultFraction(Attribute attribute) const {
    for (const fixtures::Channel& channel : mode_.channels) {
        for (const fixtures::ChannelFunction& f : channel.functions) {
            if (f.attribute != attribute || f.kind == fixtures::FunctionKind::NoFeature) continue;
            const std::uint32_t value = channel.defaultValue;
            if (f.dmxTo > f.dmxFrom && value >= f.dmxFrom && value <= f.dmxTo)
                return static_cast<float>(value - f.dmxFrom) / static_cast<float>(f.dmxTo - f.dmxFrom);
            return 0.0f;
        }
    }
    return 0.0f;
}

// The wheel slot the channel's default DMX value selects (1-based), or 1 when it selects none.
int ConsoleFixture::defaultSlot(Attribute attribute) const {
    for (const fixtures::Channel& channel : mode_.channels) {
        for (const fixtures::ChannelFunction& f : channel.functions) {
            if (f.attribute != attribute || f.kind != fixtures::FunctionKind::WheelSlot) continue;
            const std::uint32_t value = channel.defaultValue;
            if (value < f.dmxFrom || value > f.dmxTo) continue;
            if (f.dmxTo == f.dmxFrom) return static_cast<int>(f.slotFrom + 0.5f);
            const float t = static_cast<float>(value - f.dmxFrom) / static_cast<float>(f.dmxTo - f.dmxFrom);
            return static_cast<int>(f.slotFrom + t * (f.slotTo - f.slotFrom) + 0.5f);
        }
    }
    return 1;
}

void ConsoleFixture::resetToDefaultLook() {
    std::fill(bytes_.begin(), bytes_.end(), std::uint8_t{0});
    encoder_.writeDefaults(bytes_);

    values_ = ControlValues{};
    for (const fixtures::AttributeEncoder::Control& control : encoder_.controls()) {
        values_.level[index(control.attribute)] = defaultFraction(control.attribute);
        if (control.wheel != nullptr)
            values_.slot[index(control.attribute)] = static_cast<std::int16_t>(defaultSlot(control.attribute));
    }
    if (encoder_.has(Attribute::Pan)) values_.pan = values_.level[index(Attribute::Pan)];
    if (encoder_.has(Attribute::Tilt)) values_.tilt = values_.level[index(Attribute::Tilt)];

    // The "on" look: full intensity, white, shutter open - everything else stays at its default.
    if (encoder_.has(Attribute::Dimmer)) {
        encoder_.setNormalized(bytes_, Attribute::Dimmer, 1.0f);
        values_.level[index(Attribute::Dimmer)] = 1.0f;
    }
    encoder_.setColor(bytes_, glm::vec3(1.0f));
    encoder_.setShutter(bytes_, fixtures::FunctionKind::ShutterOpen);
}

void ConsoleFixture::capture() {
    if (captured_) return;
    resetToDefaultLook();
    captured_ = true;
}

void ConsoleFixture::home() {
    resetToDefaultLook();
    captured_ = true;
}

// ---------------------------------------------------------------------------
// Attributes

void ConsoleFixture::setIntensity(float t) {
    capture();
    values_.intensity = std::clamp(t, 0.0f, 1.0f);
    if (encoder_.has(Attribute::Dimmer)) {
        values_.level[index(Attribute::Dimmer)] = values_.intensity;
        encoder_.setNormalized(bytes_, Attribute::Dimmer, values_.intensity);
    } else {
        applyColour();  // no dimmer channel: scale the colour instead
    }
}

void ConsoleFixture::applyColour() {
    // Without a dimmer channel the intensity fader dims the emitters themselves.
    const float scale = encoder_.has(Attribute::Dimmer) ? 1.0f : values_.intensity;
    const glm::vec3 linear = fixtures::srgbToLinear(glm::clamp(values_.colour, 0.0f, 1.0f)) * scale;
    if (encoder_.setColor(bytes_, linear)) return;

    // No mixing channels: pick the colour-wheel slot closest to the colour.
    for (const fixtures::AttributeEncoder::Control& control : encoder_.controls()) {
        if (control.attribute != Attribute::Color1 || control.wheel == nullptr) continue;
        int best = 0;
        float bestDistance = 1e9f;
        const std::vector<fixtures::WheelSlot>& slots = control.wheel->slots;
        for (std::size_t i = 0; i < slots.size(); ++i) {
            const float distance = glm::distance(slots[i].color, linear);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = static_cast<int>(i);
            }
        }
        setWheelSlot(Attribute::Color1, best + 1);
        return;
    }
}

void ConsoleFixture::setColour(const glm::vec3& srgb) {
    capture();
    values_.colour = srgb;
    applyColour();
}

void ConsoleFixture::setPosition(float pan, float tilt, float panFine, float tiltFine) {
    capture();
    values_.pan = std::clamp(pan, 0.0f, 1.0f);
    values_.tilt = std::clamp(tilt, 0.0f, 1.0f);
    values_.panFine = std::clamp(panFine, -1.0f, 1.0f);
    values_.tiltFine = std::clamp(tiltFine, -1.0f, 1.0f);
    const auto withFine = [](float coarse, float fine) { return std::clamp(coarse + fine * kFineRange, 0.0f, 1.0f); };
    if (encoder_.has(Attribute::Pan)) {
        encoder_.setNormalized(bytes_, Attribute::Pan, withFine(values_.pan, values_.panFine));
        values_.level[index(Attribute::Pan)] = values_.pan;
    }
    if (encoder_.has(Attribute::Tilt)) {
        encoder_.setNormalized(bytes_, Attribute::Tilt, withFine(values_.tilt, values_.tiltFine));
        values_.level[index(Attribute::Tilt)] = values_.tilt;
    }
}

void ConsoleFixture::setLevel(Attribute attribute, float t) {
    capture();
    values_.level[index(attribute)] = std::clamp(t, 0.0f, 1.0f);
    encoder_.setNormalized(bytes_, attribute, values_.level[index(attribute)]);
}

void ConsoleFixture::setWheelSlot(Attribute attribute, int slot) {
    capture();
    values_.slot[index(attribute)] = static_cast<std::int16_t>(slot);
    encoder_.setWheelSlot(bytes_, attribute, static_cast<float>(slot));
}

void ConsoleFixture::setShutter(ShutterMode mode, float strobeHz) {
    capture();
    values_.shutter = mode;
    values_.strobeHz = strobeHz;
    switch (mode) {
        case ShutterMode::Open:
            encoder_.setShutter(bytes_, fixtures::FunctionKind::ShutterOpen);
            break;
        case ShutterMode::Closed:
            encoder_.setShutter(bytes_, fixtures::FunctionKind::ShutterClosed);
            break;
        case ShutterMode::Strobe:
            encoder_.setShutter(bytes_, fixtures::FunctionKind::Strobe, strobeHz);
            break;
    }
}

// ---------------------------------------------------------------------------
// Programmer

void ConsoleFixture::send(ProgrammerModel& model) const {
    if (!captured_ || !patched()) return;
    for (std::size_t i = 0; i < bytes_.size(); ++i)
        model.setChannel(static_cast<dmx::UniverseId>(patch_.universe), static_cast<int>(patch_.address + i), bytes_[i]);
}

void ConsoleFixture::release(ProgrammerModel& model) {
    if (patched())
        model.release(static_cast<dmx::UniverseId>(patch_.universe), static_cast<int>(patch_.address), footprint());
    captured_ = false;
}

void ConsoleFixture::repatch(stage::DmxPatch patch, ProgrammerModel& model) {
    if (patch == patch_) return;
    if (captured_ && patched())
        model.release(static_cast<dmx::UniverseId>(patch_.universe), static_cast<int>(patch_.address), footprint());
    patch_ = patch;
    send(model);
}

}  // namespace dmxviz::ui
