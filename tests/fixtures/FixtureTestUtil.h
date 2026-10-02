#pragma once
// Builders for small fixture types used by the fixture tests.

#include "fixtures/FixtureType.h"
#include "fixtures/GeometryBuilder.h"

#include <string>
#include <utility>
#include <vector>

namespace dmxviz::fixtures::test {

inline ChannelFunction fn(Attribute a, std::uint32_t from, std::uint32_t to, float physicalFrom, float physicalTo) {
    ChannelFunction f;
    f.attribute = a;
    f.kind = attributeInfo(a).defaultKind;
    f.dmxFrom = from;
    f.dmxTo = to;
    f.physicalFrom = physicalFrom;
    f.physicalTo = physicalTo;
    return f;
}

inline ChannelFunction kindFn(Attribute a, FunctionKind kind, std::uint32_t from, std::uint32_t to, float pFrom = 0,
                              float pTo = 0) {
    ChannelFunction f = fn(a, from, to, pFrom, pTo);
    f.kind = kind;
    return f;
}

inline ChannelFunction slotFn(Attribute a, const std::string& wheel, std::uint32_t from, std::uint32_t to, float slotFrom,
                              float slotTo) {
    ChannelFunction f = fn(a, from, to, 0, 0);
    f.kind = FunctionKind::WheelSlot;
    f.wheel = wheel;
    f.slotFrom = slotFrom;
    f.slotTo = slotTo;
    return f;
}

inline Channel channel(std::string name, std::vector<std::uint16_t> offsets, std::string geometry,
                       std::vector<ChannelFunction> functions, std::uint32_t defaultValue = 0) {
    Channel c;
    c.name = std::move(name);
    c.offsets = std::move(offsets);
    c.geometry = std::move(geometry);
    c.functions = std::move(functions);
    c.defaultValue = defaultValue;
    return c;
}

inline WheelSlot colorSlot(std::string name, glm::vec3 color) {
    WheelSlot s;
    s.kind = SlotKind::Color;
    s.name = std::move(name);
    s.color = color;
    return s;
}

inline WheelSlot openSlot() {
    WheelSlot s;
    s.name = "Open";
    return s;
}

// A tiny but valid SVG gobo (white dot on black).
inline std::string dotSvg() {
    return R"(<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">)"
           R"(<rect width="64" height="64" fill="#000"/><circle cx="32" cy="32" r="12" fill="#fff"/></svg>)";
}

// Moving head spot with 16-bit pan/tilt, dimmer, shutter/strobe, colour wheel,
// gobo wheel with rotation, prism, zoom, iris, frost and CMY.
inline FixtureType makeTestSpot() {
    using A = Attribute;
    FixtureType t;
    t.id = "test/spot";
    t.manufacturer = "Test";
    t.name = "Spot";

    GeometryRecipe recipe;
    recipe.movingHead = true;
    recipe.size = {0.4f, 0.6f, 0.3f};
    recipe.beam.type = BeamType::Spot;
    recipe.beam.beamAngle = degToRad(20.0f);
    recipe.beam.fieldAngle = degToRad(24.0f);
    t.geometry = buildFixtureGeometry(recipe);

    const std::string svg = dotSvg();
    t.resources.push_back({"dots", "svg", std::vector<std::uint8_t>(svg.begin(), svg.end())});

    t.wheels.push_back({"Colors",
                        {openSlot(), colorSlot("Red", {1, 0, 0}), colorSlot("Green", {0, 1, 0}),
                         colorSlot("Blue", {0, 0, 1})}});
    WheelSlot gobo;
    gobo.kind = SlotKind::Gobo;
    gobo.name = "Dots";
    gobo.image = "dots";
    t.wheels.push_back({"Gobos", {openSlot(), gobo}});
    WheelSlot prism;
    prism.kind = SlotKind::Prism;
    prism.name = "3-facet";
    prism.facets = makeCircularPrismFacets(3, degToRad(5.0f));
    t.wheels.push_back({"Prism", {openSlot(), prism}});

    DmxMode m;
    m.name = "Test";
    m.channels.push_back(channel("Pan", {1, 2}, "Yoke", {fn(A::Pan, 0, 65535, degToRad(-270), degToRad(270))}, 32768));
    m.channels.push_back(channel("Tilt", {3, 4}, "Head", {fn(A::Tilt, 0, 65535, degToRad(-135), degToRad(135))}, 32768));
    m.channels.push_back(channel("Dimmer", {5}, "", {fn(A::Dimmer, 0, 255, 0, 1)}));
    m.channels.push_back(channel("Shutter", {6}, "",
                                 {kindFn(A::Shutter1, FunctionKind::ShutterClosed, 0, 31),
                                  kindFn(A::Shutter1, FunctionKind::ShutterOpen, 32, 63),
                                  fn(A::Shutter1Strobe, 64, 127, 1, 20), fn(A::Shutter1StrobeRandom, 128, 191, 1, 20),
                                  kindFn(A::Shutter1, FunctionKind::ShutterOpen, 192, 255)},
                                 32));
    m.channels.push_back(channel("Color", {7}, "",
                                 {slotFn(A::Color1, "Colors", 0, 9, 1, 1), slotFn(A::Color1, "Colors", 10, 19, 2, 2),
                                  slotFn(A::Color1, "Colors", 20, 29, 3, 3), slotFn(A::Color1, "Colors", 30, 39, 4, 4),
                                  fn(A::Color1WheelSpin, 40, 255, 0, degToRad(360))}));
    m.channels.push_back(channel("Gobo", {8}, "",
                                 {slotFn(A::Gobo1, "Gobos", 0, 9, 1, 1), slotFn(A::Gobo1, "Gobos", 10, 255, 2, 2)}));
    m.channels.push_back(channel("Gobo Rot", {9}, "",
                                 {fn(A::Gobo1Pos, 0, 127, 0, degToRad(360)),
                                  fn(A::Gobo1PosRotate, 128, 255, degToRad(-180), degToRad(180))}));
    m.channels.push_back(channel("Prism", {10}, "",
                                 {slotFn(A::Prism1, "Prism", 0, 127, 1, 1), slotFn(A::Prism1, "Prism", 128, 255, 2, 2)}));
    m.channels.push_back(channel("Prism Rot", {11}, "", {fn(A::Prism1PosRotate, 0, 255, 0, degToRad(360))}));
    m.channels.push_back(channel("Zoom", {12}, "", {fn(A::Zoom, 0, 255, degToRad(10), degToRad(40))}));
    m.channels.push_back(channel("Iris", {13}, "", {fn(A::Iris, 0, 255, 1, 0.2f)}));
    m.channels.push_back(channel("Frost", {14}, "", {fn(A::Frost1, 0, 255, 0, 1)}));
    m.channels.push_back(channel("Cyan", {15}, "", {fn(A::ColorSub_C, 0, 255, 0, 1)}));
    m.channels.push_back(channel("Magenta", {16}, "", {fn(A::ColorSub_M, 0, 255, 0, 1)}));
    m.channels.push_back(channel("Yellow", {17}, "", {fn(A::ColorSub_Y, 0, 255, 0, 1)}));
    m.footprint = 17;
    t.modes.push_back(std::move(m));
    return t;
}

// LED par: R, G, B, W (no dimmer channel).
inline FixtureType makeTestRgbwPar() {
    using A = Attribute;
    FixtureType t;
    t.id = "test/rgbw-par";
    t.manufacturer = "Test";
    t.name = "RGBW Par";
    GeometryRecipe recipe;
    recipe.size = {0.25f, 0.12f, 0.25f};
    t.geometry = buildFixtureGeometry(recipe);
    DmxMode m;
    m.name = "4ch";
    m.channels.push_back(channel("Red", {1}, "", {fn(A::ColorAdd_R, 0, 255, 0, 1)}));
    m.channels.push_back(channel("Green", {2}, "", {fn(A::ColorAdd_G, 0, 255, 0, 1)}));
    m.channels.push_back(channel("Blue", {3}, "", {fn(A::ColorAdd_B, 0, 255, 0, 1)}));
    m.channels.push_back(channel("White", {4}, "", {fn(A::ColorAdd_W, 0, 255, 0, 1)}));
    m.footprint = 4;
    t.modes.push_back(std::move(m));
    return t;
}

// Pixel bar: master dimmer + RGB per cell.
inline FixtureType makeTestPixelBar(int cells) {
    using A = Attribute;
    FixtureType t;
    t.id = "test/pixel-bar";
    t.manufacturer = "Test";
    t.name = "Pixel Bar";
    GeometryRecipe recipe;
    recipe.size = {1.0f, 0.08f, 0.1f};
    for (int i = 1; i <= cells; ++i) recipe.pixels.push_back({std::to_string(i), {i, 1, 1}});
    t.geometry = buildFixtureGeometry(recipe);
    DmxMode m;
    m.name = "Pixels";
    m.channels.push_back(channel("Dimmer", {1}, "", {fn(A::Dimmer, 0, 255, 0, 1)}));
    std::uint16_t offset = 2;
    for (int i = 1; i <= cells; ++i) {
        const std::string g = pixelGeometryName(std::to_string(i));
        m.channels.push_back(channel("Red " + std::to_string(i), {offset++}, g, {fn(A::ColorAdd_R, 0, 255, 0, 1)}));
        m.channels.push_back(channel("Green " + std::to_string(i), {offset++}, g, {fn(A::ColorAdd_G, 0, 255, 0, 1)}));
        m.channels.push_back(channel("Blue " + std::to_string(i), {offset++}, g, {fn(A::ColorAdd_B, 0, 255, 0, 1)}));
    }
    m.footprint = offset - 1;
    t.modes.push_back(std::move(m));
    return t;
}

}  // namespace dmxviz::fixtures::test
