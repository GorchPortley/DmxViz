#pragma once
// AttributeEncoder: drive a fixture by attribute instead of by raw channel.
//
// The test console (FR-CON-1) shows "Pan 30°", "Gobo 1: slot 3", "Strobe 5 Hz",
// a colour picker... and needs to know which DMX bytes produce that. This
// class lists the attributes a mode offers and writes the matching values into
// a footprint buffer (the same bytes FixtureRuntime::setDmx() reads).
//
// All setters take the footprint *in its current state*: channel functions
// switched by a mode master are chosen according to the master's current
// value (and the master is moved only if no function fits).

#include "fixtures/FixtureType.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace dmxviz::fixtures {

class AttributeEncoder {
public:
    // Keeps references: type and mode must outlive the encoder.
    AttributeEncoder(const FixtureType& type, const DmxMode& mode);

    // One controllable attribute on one channel.
    struct Control {
        Attribute attribute = Attribute::NoFeature;
        std::string_view label;     // attribute label (canonical or preserved name)
        int channel = -1;           // index into mode.channels
        std::string_view geometry;  // geometry / group the channel controls ("" = whole fixture)
        float physicalMin = 0.0f;   // union of the physical ranges (memory units)
        float physicalMax = 0.0f;
        const Wheel* wheel = nullptr;  // for wheel-slot attributes
    };
    const FixtureType& type() const { return type_; }
    const DmxMode& mode() const { return mode_; }
    const std::vector<Control>& controls() const { return controls_; }
    // Distinct attributes of the mode, in channel order.
    std::vector<Attribute> attributes() const;
    bool has(Attribute attribute) const;

    // Writes the value making `attribute` take `value` (memory units: radians,
    // 0..1, Hz...). geometry filters channels ("" = every channel with the attribute,
    // e.g. all cells). Values outside every range are clamped. False if nothing matched.
    bool setPhysical(std::span<std::uint8_t> footprint, Attribute attribute, float value,
                     std::string_view geometry = {}) const;
    // t = 0..1 across the first function of the attribute (fader-style).
    bool setNormalized(std::span<std::uint8_t> footprint, Attribute attribute, float t,
                       std::string_view geometry = {}) const;
    // Selects wheel position `slot` (1-based; 2.5 = split) through a WheelSlot function.
    bool setWheelSlot(std::span<std::uint8_t> footprint, Attribute attribute, float slot,
                      std::string_view geometry = {}) const;
    // Shutter open/closed or a strobe variant at `frequency` Hz (0 = any).
    bool setShutter(std::span<std::uint8_t> footprint, FunctionKind kind, float frequency = 0.0f,
                    std::string_view geometry = {}) const;
    // Colour picker: sets the mode's additive emitters (RGB, + W extraction) or
    // CMY flags to approximate `linearRgb` (max component = 1 is full saturation level).
    bool setColor(std::span<std::uint8_t> footprint, const glm::vec3& linearRgb, std::string_view geometry = {}) const;

    void writeDefaults(std::span<std::uint8_t> footprint) const;
    void writeHighlight(std::span<std::uint8_t> footprint) const;  // highlight values, defaults elsewhere
    static void writeChannel(std::span<std::uint8_t> footprint, const Channel& channel, std::uint32_t value);
    std::uint32_t readChannel(std::span<const std::uint8_t> footprint, const Channel& channel) const;

private:
    // Picks the function of `channel` with the best score (score(f) < 0 rejects),
    // preferring ones whose mode master currently allows them; moves the master
    // into range if only a switched-off function fits. Returns the index or -1.
    template <typename Score>
    int pickFunction(std::span<std::uint8_t> footprint, const Channel& channel, Score&& score) const;
    bool geometryMatches(const Channel& channel, std::string_view geometry) const;

    const FixtureType& type_;
    const DmxMode& mode_;
    std::vector<Control> controls_;
};

}  // namespace dmxviz::fixtures
