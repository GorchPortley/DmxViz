#pragma once
// ConsoleFixture: one fixture as the test console controls it.
//
// It holds the fixture's footprint bytes (the DMX values of its channels) and the control
// values the UI shows (intensity, colour, pan/tilt, ...). Setters change the attribute through
// the fixture library's AttributeEncoder, so the same call works for an RGB par, a CMY
// mover or a colour-wheel spot. The first change "captures" the fixture: it starts from its
// default look with shutter open, full intensity and white light, so a fixture you touch is
// visible at once. send() hands the bytes to the programmer (via ProgrammerModel).
//
// No ImGui in here: it is plain logic with unit tests.

#include "core/Id.h"
#include "fixtures/Attribute.h"
#include "fixtures/AttributeEncoder.h"
#include "fixtures/FixtureType.h"
#include "stage/NodeContent.h"
#include "ui/ProgrammerModel.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace dmxviz::ui {

enum class ShutterMode : std::uint8_t { Open, Closed, Strobe };

// What the console's controls show for a fixture. Faders are 0..1 across the attribute's range.
struct ControlValues {
    float intensity = 1.0f;
    glm::vec3 colour{1.0f};  // sRGB, as the colour picker shows it
    float pan = 0.5f;
    float tilt = 0.5f;
    float panFine = 0.0f;  // -1..1, a small offset on top of pan / tilt
    float tiltFine = 0.0f;
    ShutterMode shutter = ShutterMode::Open;
    float strobeHz = 5.0f;
    std::array<float, fixtures::kAttributeCount> level{};         // other faders, per attribute
    std::array<std::int16_t, fixtures::kAttributeCount> slot{};   // wheel slot per wheel attribute (1-based, 0 = unknown)
};

class ConsoleFixture {
public:
    // Fine pan/tilt moves the position by at most this fraction of the full range.
    static constexpr float kFineRange = 0.02f;

    // `type` and `mode` must outlive the object (they live in the fixture library).
    ConsoleFixture(NodeId node, const fixtures::FixtureType& type, const fixtures::DmxMode& mode,
                   stage::DmxPatch patch);

    NodeId node() const { return node_; }
    const fixtures::FixtureType& type() const { return type_; }
    const fixtures::DmxMode& mode() const { return mode_; }
    const fixtures::AttributeEncoder& encoder() const { return encoder_; }
    const ControlValues& values() const { return values_; }
    stage::DmxPatch patch() const { return patch_; }
    int footprint() const { return mode_.footprint; }
    // The programmer holds this fixture's channels.
    bool captured() const { return captured_; }
    // Can be sent to a DMX address.
    bool patched() const {
        return patch_.patched() && patch_.universe <= 0xFFFFu &&
               patch_.address + static_cast<std::uint32_t>(footprint()) - 1 <= dmx::kUniverseSize;
    }

    // ---- attribute changes (capture the fixture first; call send() afterwards) ----------------
    void setIntensity(float t);
    // sRGB colour. Mixes RGB(W) or CMY; without those it picks the nearest colour-wheel slot.
    void setColour(const glm::vec3& srgb);
    void setPosition(float pan, float tilt, float panFine, float tiltFine);
    // A fader-style attribute (zoom, iris, wheel rotation ...), t = 0..1.
    void setLevel(fixtures::Attribute attribute, float t);
    void setWheelSlot(fixtures::Attribute attribute, int slot);
    void setShutter(ShutterMode mode, float strobeHz);
    // Back to the default look (see class comment) and into the programmer.
    void home();

    // ---- programmer -----------------------------------------------------------------------------
    // Writes the footprint to the programmer.
    void send(ProgrammerModel& model) const;
    // Takes the fixture out of the programmer again.
    void release(ProgrammerModel& model);
    // The fixture was re-patched: moves its programmer values to the new address.
    void repatch(stage::DmxPatch patch, ProgrammerModel& model);

private:
    void capture();
    void resetToDefaultLook();
    void applyColour();
    float defaultFraction(fixtures::Attribute attribute) const;
    int defaultSlot(fixtures::Attribute attribute) const;
    static std::size_t index(fixtures::Attribute attribute) { return static_cast<std::size_t>(attribute); }

    NodeId node_;
    const fixtures::FixtureType& type_;
    const fixtures::DmxMode& mode_;
    fixtures::AttributeEncoder encoder_;
    stage::DmxPatch patch_;
    std::vector<std::uint8_t> bytes_;
    ControlValues values_;
    bool captured_ = false;
};

}  // namespace dmxviz::ui
