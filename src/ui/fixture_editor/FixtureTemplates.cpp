#include "ui/fixture_editor/FixtureTemplates.h"

#include "fixtures/GeometryBuilder.h"
#include "ui/fixture_editor/ChannelEditing.h"

#include <string>
#include <vector>

namespace dmxviz::ui::fixture_editor {

using fixtures::Attribute;
using fixtures::Channel;
using fixtures::DmxMode;
using fixtures::FixtureType;
using fixtures::FunctionKind;
using fixtures::SlotKind;
using fixtures::Wheel;
namespace names = fixtures::geometry_names;

namespace {

// Gobo images as tiny SVG files: white passes the light, black blocks it.
constexpr const char* kDotsSvg =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"256\" height=\"256\" viewBox=\"0 0 256 256\">"
    "<rect width=\"256\" height=\"256\" fill=\"#000\"/>"
    "<circle cx=\"128\" cy=\"128\" r=\"24\" fill=\"#fff\"/><circle cx=\"128\" cy=\"52\" r=\"20\" fill=\"#fff\"/>"
    "<circle cx=\"204\" cy=\"128\" r=\"20\" fill=\"#fff\"/><circle cx=\"128\" cy=\"204\" r=\"20\" fill=\"#fff\"/>"
    "<circle cx=\"52\" cy=\"128\" r=\"20\" fill=\"#fff\"/></svg>";

constexpr const char* kStarSvg =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"256\" height=\"256\" viewBox=\"0 0 256 256\">"
    "<rect width=\"256\" height=\"256\" fill=\"#000\"/>"
    "<polygon points=\"128,24 154,98 232,98 169,144 193,220 128,174 63,220 87,144 24,98 102,98\" fill=\"#fff\"/></svg>";

std::vector<std::uint8_t> bytesOf(const char* text) {
    const std::string s(text);
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

void setIdentity(FixtureType& type, const char* name, const char* shortName, const char* description) {
    type.manufacturer = "Custom";
    type.name = name;
    type.shortName = shortName;
    type.description = description;
    type.source = fixtures::FixtureSource::Native;
    type.id = fixtures::makeFixtureId(type.manufacturer, type.name);
}

Channel& add(DmxMode& mode, const char* name, Attribute attribute, int bytes, const char* geometry, std::uint32_t defaultValue = 0) {
    Channel& channel = appendChannel(mode, makeChannel(name, attribute, bytes, 1, geometry));
    channel.defaultValue = defaultValue;
    return channel;
}

// Closed / open / strobe on one channel, like most real fixtures.
void addShutter(DmxMode& mode, const char* geometry) {
    Channel& shutter = add(mode, "Shutter / Strobe", Attribute::Shutter1, 1, geometry, 32);
    shutter.functions.clear();
    auto part = [&](const char* name, Attribute attribute, FunctionKind kind, std::uint32_t from, std::uint32_t to) {
        fixtures::ChannelFunction f = makeFunction(attribute, from, to);
        f.name = name;
        f.kind = kind;
        shutter.functions.push_back(f);
    };
    part("Closed", Attribute::Shutter1, FunctionKind::ShutterClosed, 0, 31);
    part("Open", Attribute::Shutter1, FunctionKind::ShutterOpen, 32, 63);
    part("Strobe slow to fast", Attribute::Shutter1Strobe, FunctionKind::Strobe, 64, 255);
}

void addDimmer(DmxMode& mode, const char* geometry) {
    Channel& dimmer = add(mode, "Dimmer", Attribute::Dimmer, 1, geometry);
    dimmer.highlightValue = 255;
}

// Position within a rotating holder: index angle first, then spin in both directions.
void addRotation(DmxMode& mode, const char* name, Attribute index, Attribute spin, const char* geometry) {
    Channel& channel = add(mode, name, index, 1, geometry);
    channel.functions.clear();
    fixtures::ChannelFunction indexFn = makeFunction(index, 0, 127);
    indexFn.name = "Index 0-360 degrees";
    indexFn.physicalFrom = 0.0f;
    indexFn.physicalTo = degToRad(360.0f);
    fixtures::ChannelFunction spinFn = makeFunction(spin, 128, 255);
    spinFn.name = "Spin";
    spinFn.physicalFrom = degToRad(-180.0f);
    spinFn.physicalTo = degToRad(180.0f);
    channel.functions = {indexFn, spinFn};
}

void addWheelChannel(FixtureType& type, DmxMode& mode, const char* name, Attribute attribute, const char* wheelName,
                     const char* geometry) {
    Channel& channel = add(mode, name, attribute, 1, geometry);
    if (const Wheel* wheel = type.findWheel(wheelName)) fillWheelFunctions(channel, *wheel, attribute);
}

FixtureType makeBlank() {
    FixtureType type;
    setIdentity(type, "New Fixture", "New", "");
    fixtures::GeometryRecipe recipe;
    recipe.size = glm::vec3(0.2f, 0.2f, 0.2f);
    recipe.beam.type = fixtures::BeamType::Wash;
    type.geometry = fixtures::buildFixtureGeometry(recipe);

    DmxMode mode;
    mode.name = "Default";
    addDimmer(mode, names::kBeam);
    type.modes.push_back(std::move(mode));
    return type;
}

FixtureType makeLedPar() {
    FixtureType type;
    setIdentity(type, "LED Par", "Par", "RGBW LED wash par.");
    type.categories = {"Color Changer"};
    type.physical.weight = 3.0f;
    type.physical.power = 60.0f;
    type.physical.dimensions = glm::vec3(0.2f, 0.22f, 0.2f);

    fixtures::GeometryRecipe recipe;
    recipe.size = type.physical.dimensions;
    recipe.beam.type = fixtures::BeamType::Wash;
    recipe.beam.lensRadius = 0.08f;
    recipe.beam.beamAngle = degToRad(38.0f);
    recipe.beam.fieldAngle = degToRad(52.0f);
    recipe.beam.luminousFlux = 3000.0f;
    type.geometry = fixtures::buildFixtureGeometry(recipe);

    DmxMode mode;
    mode.name = "6 channel";
    addDimmer(mode, names::kBeam);
    add(mode, "Red", Attribute::ColorAdd_R, 1, names::kBeam);
    add(mode, "Green", Attribute::ColorAdd_G, 1, names::kBeam);
    add(mode, "Blue", Attribute::ColorAdd_B, 1, names::kBeam);
    add(mode, "White", Attribute::ColorAdd_W, 1, names::kBeam);
    addShutter(mode, names::kBeam);
    type.modes.push_back(std::move(mode));
    return type;
}

FixtureType makeMovingHead() {
    FixtureType type;
    setIdentity(type, "Moving Head", "MH", "Spot moving head with colour wheel, gobos and prism.");
    type.categories = {"Moving Head"};
    type.physical.weight = 18.0f;
    type.physical.power = 300.0f;
    type.physical.dimensions = glm::vec3(0.32f, 0.5f, 0.28f);

    fixtures::GeometryRecipe recipe;
    recipe.movingHead = true;
    recipe.size = type.physical.dimensions;
    recipe.beam.type = fixtures::BeamType::Spot;
    recipe.beam.lensRadius = 0.06f;
    recipe.beam.beamAngle = degToRad(15.0f);
    recipe.beam.fieldAngle = degToRad(22.0f);
    recipe.beam.luminousFlux = 8000.0f;
    type.geometry = fixtures::buildFixtureGeometry(recipe);

    // Wheels. Slot 1 of every wheel is the open position.
    Wheel colors;
    colors.name = "Color wheel";
    colors.slots.push_back(makeSlot(SlotKind::Open, 1));
    const struct {
        const char* name;
        glm::vec3 color;
    } filters[] = {{"Red", {1.0f, 0.03f, 0.03f}},     {"Green", {0.03f, 0.8f, 0.05f}}, {"Blue", {0.03f, 0.08f, 1.0f}},
                   {"Yellow", {1.0f, 0.7f, 0.02f}},   {"Magenta", {0.9f, 0.03f, 0.6f}}};
    for (const auto& filter : filters) {
        fixtures::WheelSlot slot = makeSlot(SlotKind::Color, 1);
        slot.name = filter.name;
        slot.color = filter.color;
        colors.slots.push_back(slot);
    }
    type.wheels.push_back(std::move(colors));

    Wheel gobos;
    gobos.name = "Gobo wheel";
    gobos.slots.push_back(makeSlot(SlotKind::Open, 1));
    const std::string dots = addResource(type, "dots", "svg", bytesOf(kDotsSvg));
    const std::string star = addResource(type, "star", "svg", bytesOf(kStarSvg));
    const std::string goboImages[] = {dots, star};
    const char* goboNames[] = {"Dots", "Star"};
    for (int i = 0; i < 2; ++i) {
        fixtures::WheelSlot slot = makeSlot(SlotKind::Gobo, i + 1);
        slot.name = goboNames[i];
        slot.image = goboImages[i];
        gobos.slots.push_back(slot);
    }
    type.wheels.push_back(std::move(gobos));

    Wheel prism;
    prism.name = "Prism wheel";
    prism.slots.push_back(makeSlot(SlotKind::Open, 1));
    fixtures::WheelSlot threeFacet = makeSlot(SlotKind::Prism, 1);
    threeFacet.name = "3-facet";
    prism.slots.push_back(threeFacet);
    type.wheels.push_back(std::move(prism));

    DmxMode mode;
    mode.name = "Standard";
    add(mode, "Pan", Attribute::Pan, 2, names::kYoke, 32768);
    add(mode, "Tilt", Attribute::Tilt, 2, names::kHead, 32768);
    addShutter(mode, names::kBeam);
    addDimmer(mode, names::kBeam);
    add(mode, "Zoom", Attribute::Zoom, 1, names::kBeam, 128);
    addWheelChannel(type, mode, "Color wheel", Attribute::Color1, "Color wheel", names::kBeam);
    addWheelChannel(type, mode, "Gobo wheel", Attribute::Gobo1, "Gobo wheel", names::kBeam);
    addRotation(mode, "Gobo rotation", Attribute::Gobo1Pos, Attribute::Gobo1PosRotate, names::kBeam);
    addWheelChannel(type, mode, "Prism", Attribute::Prism1, "Prism wheel", names::kBeam);
    addRotation(mode, "Prism rotation", Attribute::Prism1Pos, Attribute::Prism1PosRotate, names::kBeam);
    type.modes.push_back(std::move(mode));
    return type;
}

}  // namespace

const char* templateName(FixtureTemplate kind) {
    switch (kind) {
        case FixtureTemplate::Blank: return "Blank";
        case FixtureTemplate::LedPar: return "LED par";
        case FixtureTemplate::MovingHead: return "Moving head";
    }
    return "Blank";
}

FixtureType makeTemplateFixture(FixtureTemplate kind) {
    switch (kind) {
        case FixtureTemplate::Blank: return makeBlank();
        case FixtureTemplate::LedPar: return makeLedPar();
        case FixtureTemplate::MovingHead: return makeMovingHead();
    }
    return makeBlank();
}

}  // namespace dmxviz::ui::fixture_editor
