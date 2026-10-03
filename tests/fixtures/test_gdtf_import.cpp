#include "FixtureTestGdtf.h"
#include "assets/ImageLoader.h"
#include "fixtures/Archive.h"
#include "fixtures/ColorMath.h"
#include "fixtures/FixtureRuntime.h"
#include "fixtures/GdtfImporter.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace dmxviz;
using namespace dmxviz::fixtures;
using namespace dmxviz::fixtures::test;
using doctest::Approx;

namespace {

FixtureType importXml(const std::string& xml, std::vector<std::string>* warnings = nullptr, bool gobo = true,
                      bool model = true) {
    std::string error;
    auto t = importGdtf(makeGdtfArchive(xml, gobo, model), &error, warnings);
    INFO(error);
    REQUIRE(t.has_value());
    return std::move(*t);
}

const Channel& channelNamed(const DmxMode& mode, std::string_view name) {
    const Channel* c = mode.findChannel(name);
    INFO(name);
    REQUIRE(c != nullptr);
    return *c;
}

bool anyWarningContains(const std::vector<std::string>& warnings, std::string_view text) {
    return std::any_of(warnings.begin(), warnings.end(),
                       [&](const std::string& w) { return w.find(text) != std::string::npos; });
}

const ChannelFunction* functionAt(const Channel& c, std::uint32_t dmx, std::string_view name = {}) {
    for (const ChannelFunction& f : c.functions)
        if (f.dmxFrom <= dmx && dmx <= f.dmxTo && (name.empty() || f.name == name)) return &f;
    return nullptr;
}

void checkVec(const glm::vec3& v, float x, float y, float z) {
    CHECK(v.x == Approx(x).epsilon(1e-4).scale(1.0));
    CHECK(v.y == Approx(y).epsilon(1e-4).scale(1.0));
    CHECK(v.z == Approx(z).epsilon(1e-4).scale(1.0));
}

}  // namespace

TEST_CASE("GDTF import: metadata and physical data") {
    std::vector<std::string> warnings;
    const FixtureType t = importXml(movingHeadXml(), &warnings);
    CHECK(t.source == FixtureSource::Gdtf);
    CHECK(t.manufacturer == "Acme Lighting");
    CHECK(t.name == "Test Spot 700");
    CHECK(t.shortName == "TS700");
    CHECK(t.id == "acme-lighting/test-spot-700");
    CHECK(t.description == "Moving head for the importer tests");
    CHECK(t.physical.weight == Approx(21.5f));
    CHECK(t.physical.power == Approx(650.0f));
    REQUIRE(t.categories.size() == 1);
    CHECK(t.categories[0] == "Moving Head");
    REQUIRE(t.modes.size() == 2);
    CHECK(t.modes[0].name == "Standard");
    CHECK(t.modes[0].footprint == 13);
    CHECK(t.modes[1].name == "Reduced");
    CHECK(t.modes[1].footprint == 1);
    CHECK(validateFixtureType(t).empty());
}

TEST_CASE("GDTF import: geometry tree is converted to Y-up metres") {
    std::vector<std::string> warnings;
    const FixtureType t = importXml(movingHeadXml(), &warnings);

    // Base -> Yoke (Pan axis) -> Head (Tilt axis) -> Beam
    CHECK(t.geometry.name == "Body");
    CHECK(t.geometry.type == GeometryType::Generic);
    REQUIRE(t.geometry.children.size() == 1);
    const Geometry& yoke = t.geometry.children[0];
    CHECK(yoke.name == "Yoke");
    CHECK(yoke.type == GeometryType::Axis);
    REQUIRE(yoke.children.size() == 1);
    const Geometry& head = yoke.children[0];
    CHECK(head.name == "Head");
    CHECK(head.type == GeometryType::Axis);
    REQUIRE(head.children.size() == 2);  // beam + the display; the unknown element is skipped
    const Geometry& beam = head.children[0];
    CHECK(beam.name == "Beam");
    CHECK(beam.type == GeometryType::Beam);
    const Geometry& screen = head.children[1];
    CHECK(screen.name == "Screen");
    CHECK(screen.type == GeometryType::Generic);
    CHECK(anyWarningContains(warnings, "Bogus"));

    // GDTF (x, y, z) -> (x, z, -y): the yoke hangs 0.1 m below the base (GDTF z = -0.1) and sits 0.02 m
    // along GDTF y, which is DmxViz -z.
    checkVec(yoke.position, 0.05f, -0.1f, -0.02f);
    checkVec(head.position, 0.0f, -0.2f, 0.0f);
    checkVec(beam.position, 0.0f, -0.2f, 0.0f);
    // The head is turned 90 degrees about GDTF Z: GDTF x -> y, i.e. DmxViz x -> -z.
    checkVec(head.rotation * glm::vec3(1, 0, 0), 0.0f, 0.0f, -1.0f);
    // ... and the beam axis (-Y) is not affected by a rotation about the vertical axis.
    checkVec(head.rotation * glm::vec3(0, -1, 0), 0.0f, -1.0f, 0.0f);
    checkVec(t.geometry.rotation * glm::vec3(1, 2, 3), 1.0f, 2.0f, 3.0f);

    // Models: GDTF Length/Width/Height are DmxViz X/Z/Y.
    CHECK(yoke.model.primitive == PrimitiveShape::Yoke);
    checkVec(yoke.model.size, 0.4f, 0.3f, 0.1f);
    CHECK(head.model.primitive == PrimitiveShape::Head);
    CHECK(beam.model.primitive == PrimitiveShape::Cylinder);
    // A model file that is in the archive is kept as a resource, the primitive remains the fallback.
    CHECK(t.geometry.model.primitive == PrimitiveShape::Base);
    REQUIRE(!t.geometry.model.mesh.empty());
    const Resource* mesh = t.findResource(t.geometry.model.mesh);
    REQUIRE(mesh != nullptr);
    CHECK(mesh->format == "3ds");
    CHECK(mesh->data.size() == 13);
    // A model whose file is missing falls back to a box of the declared size.
    CHECK(screen.model.mesh.empty());
    CHECK(screen.model.primitive == PrimitiveShape::Box);
    CHECK(anyWarningContains(warnings, "not-in-archive"));

    // Beam geometry parameters.
    CHECK(beam.beam.type == BeamType::Spot);
    CHECK(beam.beam.beamAngle == Approx(degToRad(12.0f)));
    CHECK(beam.beam.fieldAngle == Approx(degToRad(22.0f)));
    CHECK(beam.beam.lensRadius == Approx(0.08f));
    CHECK(beam.beam.luminousFlux == Approx(14000.0f));
    CHECK(beam.beam.colorTemperature == Approx(7000.0f));
    CHECK(t.beamCount() == 1);
}

TEST_CASE("GDTF import: 16-bit channels and channel functions with DMX ranges") {
    const FixtureType t = importXml(movingHeadXml());
    const DmxMode& mode = t.modes[0];

    const Channel& pan = channelNamed(mode, "Yoke_Pan");
    REQUIRE(pan.offsets.size() == 2);
    CHECK(pan.offsets[0] == 1);
    CHECK(pan.offsets[1] == 2);
    CHECK(pan.byteCount() == 2);
    CHECK(pan.geometry == "Yoke");
    CHECK(pan.defaultValue == 32768);
    REQUIRE(pan.functions.size() == 1);
    CHECK(pan.functions[0].attribute == Attribute::Pan);
    CHECK(pan.functions[0].dmxFrom == 0);
    CHECK(pan.functions[0].dmxTo == 65535);
    CHECK(pan.functions[0].physicalFrom == Approx(degToRad(-270.0f)));
    CHECK(pan.functions[0].physicalTo == Approx(degToRad(270.0f)));

    const Channel& tilt = channelNamed(mode, "Head_Tilt");
    CHECK(tilt.offsets == std::vector<std::uint16_t>{3, 4});
    CHECK(tilt.geometry == "Head");

    // The movement limits come from RealFade / RealAcceleration: 540 degrees in 2.7 s.
    CHECK(t.physical.movement.panMaxSpeed == Approx(degToRad(200.0f)));
    CHECK(t.physical.movement.panAcceleration == Approx(degToRad(400.0f)));
    CHECK(t.physical.movement.tiltMaxSpeed == Approx(degToRad(200.0f)));

    const Channel& dimmer = channelNamed(mode, "Beam_Dimmer");
    CHECK(dimmer.offsets == std::vector<std::uint16_t>{5});
    CHECK(dimmer.highlightValue == 255);
    CHECK_FALSE(pan.highlightValue.has_value());
    REQUIRE(dimmer.functions.size() == 1);
    CHECK(dimmer.functions[0].physicalTo == Approx(1.0f));

    const Channel& zoom = channelNamed(mode, "Beam_Zoom");
    REQUIRE(zoom.functions.size() == 1);
    CHECK(zoom.functions[0].attribute == Attribute::Zoom);
    CHECK(zoom.functions[0].physicalFrom == Approx(degToRad(8.0f)));
    CHECK(zoom.functions[0].physicalTo == Approx(degToRad(42.0f)));

    // Shutter: two logical channels share the DMX range; every function ends where the next starts.
    const Channel& shutter = channelNamed(mode, "Beam_Shutter1");
    REQUIRE(shutter.functions.size() == 3);
    CHECK(shutter.defaultValue == 32);
    const ChannelFunction* closed = functionAt(shutter, 0);
    REQUIRE(closed != nullptr);
    CHECK(closed->kind == FunctionKind::ShutterClosed);
    CHECK(closed->dmxTo == 31);
    const ChannelFunction* open = functionAt(shutter, 40);
    REQUIRE(open != nullptr);
    CHECK(open->kind == FunctionKind::ShutterOpen);
    CHECK(open->dmxFrom == 32);
    CHECK(open->dmxTo == 63);
    const ChannelFunction* strobe = functionAt(shutter, 200);
    REQUIRE(strobe != nullptr);
    CHECK(strobe->attribute == Attribute::Shutter1Strobe);
    CHECK(strobe->kind == FunctionKind::Strobe);
    CHECK(strobe->dmxFrom == 64);
    CHECK(strobe->dmxTo == 255);
    CHECK(strobe->physicalFrom == Approx(1.0f));
    CHECK(strobe->physicalTo == Approx(20.0f));

    // A virtual channel (Offset None) has no slot; percent values become ratios.
    const Channel& virt = channelNamed(mode, "Body_Dimmer");
    CHECK(virt.offsets.empty());
    CHECK(virt.defaultValue == 255);
    CHECK(virt.functions[0].physicalTo == Approx(1.0f));

    // Attributes DmxViz does not know are kept.
    const Channel& custom = channelNamed(mode, "Body_MyCustomAttr");
    REQUIRE(custom.functions.size() == 1);
    CHECK(custom.functions[0].attribute == Attribute::Unknown);
    CHECK(custom.functions[0].attributeName == "MyCustomAttr");

    // Mode master: the effects macro only applies while the control channel is 200..255.
    const Channel& effects = channelNamed(mode, "Body_Effects1");
    REQUIRE(effects.functions.size() == 1);
    CHECK(effects.functions[0].modeMaster == "Body_Control");
    CHECK(effects.functions[0].modeFrom == 200);
    CHECK(effects.functions[0].modeTo == 255);

    // The reduced mode has its own channel list.
    CHECK(t.modes[1].channels.size() == 1);
}

TEST_CASE("GDTF import: colour wheel colours are linear RGB filters") {
    const FixtureType t = importXml(movingHeadXml());
    const Wheel* wheel = t.findWheel("Color Wheel 1");
    REQUIRE(wheel != nullptr);
    REQUIRE(wheel->slots.size() == 3);
    CHECK(wheel->slots[0].kind == SlotKind::Open);
    CHECK(wheel->slots[0].color.r == Approx(1.0f).epsilon(0.02));
    CHECK(wheel->slots[0].color.g == Approx(1.0f).epsilon(0.02));
    CHECK(wheel->slots[0].color.b == Approx(1.0f).epsilon(0.02));

    // xyY of the sRGB red primary at 21.26 % luminance is pure linear red.
    CHECK(wheel->slots[1].kind == SlotKind::Color);
    CHECK(wheel->slots[1].name == "Red");
    CHECK(wheel->slots[1].color.r == Approx(1.0f).epsilon(0.03));
    CHECK(wheel->slots[1].color.g < 0.03f);
    CHECK(wheel->slots[1].color.b < 0.03f);
    CHECK(wheel->slots[2].color.b == Approx(1.0f).epsilon(0.03));
    CHECK(wheel->slots[2].color.r < 0.03f);

    // ChannelSets with WheelSlotIndex become wheel slot functions.
    const Channel& color = channelNamed(t.modes[0], "Beam_Color1");
    REQUIRE(color.functions.size() == 4);
    const ChannelFunction* red = functionAt(color, 15);
    REQUIRE(red != nullptr);
    CHECK(red->attribute == Attribute::Color1);
    CHECK(red->kind == FunctionKind::WheelSlot);
    CHECK(red->wheel == "Color Wheel 1");
    CHECK(red->slotFrom == Approx(2.0f));
    CHECK(red->slotTo == Approx(2.0f));
    CHECK(red->dmxFrom == 10);
    CHECK(red->dmxTo == 19);
    const ChannelFunction* spin = functionAt(color, 100);
    REQUIRE(spin != nullptr);
    CHECK(spin->attribute == Attribute::Color1WheelSpin);
    CHECK(spin->kind == FunctionKind::Spin);
    CHECK(spin->physicalFrom == Approx(degToRad(-90.0f)));
    CHECK(spin->physicalTo == Approx(degToRad(90.0f)));
}

TEST_CASE("GDTF import: gobo wheel image becomes a resource") {
    std::vector<std::string> warnings;
    const FixtureType t = importXml(movingHeadXml(), &warnings);
    const Wheel* wheel = t.findWheel("Gobo Wheel 1");
    REQUIRE(wheel != nullptr);
    REQUIRE(wheel->slots.size() == 2);
    CHECK(wheel->slots[0].kind == SlotKind::Open);
    CHECK(wheel->slots[1].kind == SlotKind::Gobo);
    CHECK(wheel->slots[1].name == "Dots");
    REQUIRE(!wheel->slots[1].image.empty());

    const Resource* image = t.findResource(wheel->slots[1].image);
    REQUIRE(image != nullptr);
    CHECK(image->format == "png");
    std::string error;
    auto decoded = assets::loadImageFromMemory(image->data, image->format, {}, &error);
    INFO(error);
    REQUIRE(decoded.has_value());
    CHECK(decoded->width == 16);
    CHECK(decoded->height == 16);

    const Channel& gobo = channelNamed(t.modes[0], "Beam_Gobo1");
    const ChannelFunction* dots = functionAt(gobo, 20);
    REQUIRE(dots != nullptr);
    CHECK(dots->kind == FunctionKind::WheelSlot);
    CHECK(dots->slotFrom == Approx(2.0f));
    CHECK(dots->dmxFrom == 16);
    CHECK(dots->dmxTo == 39);

    // Without the file the slot stays a gobo but has no image, and the import warns.
    std::vector<std::string> missingWarnings;
    const FixtureType noImage = importXml(movingHeadXml(), &missingWarnings, false);
    CHECK(noImage.findWheel("Gobo Wheel 1")->slots[1].image.empty());
    CHECK(anyWarningContains(missingWarnings, "dots"));
    CHECK(validateFixtureType(noImage).empty());
}

TEST_CASE("GDTF import: prism facets become angular offsets") {
    const FixtureType t = importXml(movingHeadXml());
    const Wheel* wheel = t.findWheel("Prism Wheel");
    REQUIRE(wheel != nullptr);
    REQUIRE(wheel->slots.size() == 2);
    CHECK(wheel->slots[0].kind == SlotKind::Open);
    const WheelSlot& prism = wheel->slots[1];
    CHECK(prism.kind == SlotKind::Prism);
    REQUIRE(prism.facets.size() == 3);

    // Each facet is tilted 5 degrees away from the axis; the offsets are along DmxViz X and Z.
    const float five = degToRad(5.0f);
    for (const glm::vec2& f : prism.facets) CHECK(glm::length(f) == Approx(five).epsilon(0.002));
    CHECK(prism.facets[0].x == Approx(0.0f).scale(1.0).epsilon(1e-4));
    CHECK(prism.facets[0].y == Approx(-five).epsilon(0.002));
    CHECK(prism.facets[1].x == Approx(-five).epsilon(0.002));
    CHECK(prism.facets[1].y == Approx(0.0f).scale(1.0).epsilon(1e-4));
    CHECK(prism.facets[2].y == Approx(five).epsilon(0.002));

    const Channel& channel = channelNamed(t.modes[0], "Beam_Prism1");
    const ChannelFunction* in = functionAt(channel, 200);
    REQUIRE(in != nullptr);
    CHECK(in->attribute == Attribute::Prism1);
    CHECK(in->slotFrom == Approx(2.0f));
}

TEST_CASE("GDTF import: GeometryReference expands to cells with their own DMX offsets") {
    std::vector<std::string> warnings;
    const FixtureType t = importXml(pixelBarXml(), &warnings, false, false);
    CHECK(validateFixtureType(t).empty());
    CHECK(t.name == "Pixel Bar 4");
    CHECK(t.manufacturer == "Acme Lighting");

    // Body with four copies of the "Cell" geometry (the template itself is not part of the tree).
    CHECK(t.geometry.name == "Body");
    REQUIRE(t.geometry.children.size() == 4);
    const float xs[4] = {-0.15f, -0.05f, 0.05f, 0.15f};
    for (std::size_t i = 0; i < 4; ++i) {
        const Geometry& cell = t.geometry.children[i];
        CHECK(cell.name == "Cell " + std::to_string(i + 1));
        checkVec(cell.position, xs[i], -0.04f, 0.0f);  // GDTF (x, 0, -0.04) -> (x, -0.04, 0)
        CHECK(cell.model.primitive == PrimitiveShape::Box);
        REQUIRE(cell.children.size() == 1);
        const Geometry& beam = cell.children[0];
        CHECK(beam.name == cell.name + " CellBeam");
        CHECK(beam.type == GeometryType::Beam);
        CHECK(beam.beam.type == BeamType::Rectangle);
        CHECK(beam.beam.emitterSize.x > beam.beam.emitterSize.y);
        checkVec(beam.position, 0.0f, -0.01f, 0.0f);
    }
    CHECK(t.beamCount() == 4);

    // 1 master dimmer + 4 cells x 3 colours, cell k starts at slot 2 + 3k.
    REQUIRE(t.modes.size() == 1);
    const DmxMode& mode = t.modes[0];
    CHECK(mode.channels.size() == 13);
    CHECK(mode.footprint == 13);
    CHECK(channelNamed(mode, "Body_Dimmer").offsets == std::vector<std::uint16_t>{1});
    for (int cell = 0; cell < 4; ++cell) {
        const std::string prefix = "Cell " + std::to_string(cell + 1) + " CellBeam_";
        const std::uint16_t first = static_cast<std::uint16_t>(2 + 3 * cell);
        const Channel& r = channelNamed(mode, prefix + "ColorAdd_R");
        const Channel& g = channelNamed(mode, prefix + "ColorAdd_G");
        const Channel& b = channelNamed(mode, prefix + "ColorAdd_B");
        CHECK(r.offsets == std::vector<std::uint16_t>{first});
        CHECK(g.offsets == std::vector<std::uint16_t>{static_cast<std::uint16_t>(first + 1)});
        CHECK(b.offsets == std::vector<std::uint16_t>{static_cast<std::uint16_t>(first + 2)});
        CHECK(r.geometry == "Cell " + std::to_string(cell + 1) + " CellBeam");
    }

    // The emitter named by the function is imported (hue only, largest component 1).
    const Emitter* red = t.findEmitter("Red");
    REQUIRE(red != nullptr);
    CHECK(red->dominantWavelength == Approx(620.0f));
    CHECK(maxComponent(red->color) == Approx(1.0f));
    CHECK(red->color.r > red->color.g);
    CHECK(channelNamed(mode, "Cell 1 CellBeam_ColorAdd_R").functions[0].emitter == "Red");
}

TEST_CASE("GDTF import: imported moving head runs in the fixture runtime") {
    auto type = std::make_shared<const FixtureType>(importXml(movingHeadXml()));
    FixtureRuntime runtime(type, "Standard");
    std::vector<std::uint8_t> dmx(13, 0);
    dmx[0] = 0x80;  // pan 32768 = centre
    dmx[2] = 0x80;  // tilt centre
    dmx[4] = 255;   // dimmer
    dmx[9] = 32;    // shutter open
    runtime.setDmx(dmx);
    runtime.update(0.1f, 0.0);
    std::vector<MeshInstance> meshes;
    std::vector<BeamState> beams;
    runtime.emit(glm::mat4(1.0f), 1, meshes, beams);
    REQUIRE(beams.size() == 1);
    // Hanging fixture: the beam leaves downwards, below the base, at the converted position.
    CHECK(beams[0].direction.y == Approx(-1.0f).epsilon(0.01));
    CHECK(beams[0].position.y == Approx(-0.5f).epsilon(0.03));  // yoke 0.1 + head 0.2 + beam 0.2 below the base
    CHECK(beams[0].position.x == Approx(0.05f).epsilon(0.02));
    CHECK(beams[0].beamAngle == Approx(degToRad(8.0f)).epsilon(0.05));  // zoom DMX 0 = 8 degrees
    CHECK(beams[0].intensity > 0.9f);

    // Full pan swings the yoke to +270 degrees at the file's speed limit (200 deg/s).
    dmx[0] = 255;
    dmx[1] = 255;
    runtime.setDmx(dmx);
    for (int i = 0; i < 300; ++i) runtime.update(0.01f, 0.1 + i * 0.01);
    CHECK(std::abs(radToDeg(runtime.panAngle())) == Approx(270.0f).epsilon(0.01));
}

TEST_CASE("GDTF import: broken input is reported, not thrown") {
    std::string error;

    // not a zip at all
    const std::vector<std::uint8_t> junk = {1, 2, 3, 4, 5};
    CHECK_FALSE(importGdtf(junk, &error).has_value());
    CHECK_FALSE(error.empty());

    // zip without description.xml
    ZipWriter noDescription;
    noDescription.add("readme.txt", std::string_view("hello"));
    error.clear();
    CHECK_FALSE(importGdtf(noDescription.finish(), &error).has_value());
    CHECK(error.find("description.xml") != std::string::npos);

    // broken XML
    error.clear();
    CHECK_FALSE(importGdtf(makeGdtfArchive("<GDTF><FixtureType Name='x'>"), &error).has_value());
    CHECK(error.find("XML") != std::string::npos);

    // XML without a fixture type
    error.clear();
    CHECK_FALSE(importGdtf(makeGdtfArchive("<GDTF DataVersion=\"1.1\"/>"), &error).has_value());
    CHECK(error.find("FixtureType") != std::string::npos);

    // no DMX modes
    error.clear();
    CHECK_FALSE(
        importGdtf(makeGdtfArchive("<GDTF DataVersion=\"1.1\"><FixtureType Name=\"x\"/></GDTF>"), &error).has_value());
    CHECK(error.find("DMX mode") != std::string::npos);

    // missing file
    error.clear();
    CHECK_FALSE(importGdtfFile("/nonexistent/none.gdtf", &error).has_value());
    CHECK(error.find("none.gdtf") != std::string::npos);
}

TEST_CASE("GDTF import: unknown elements and bad references only warn") {
    const std::string xml = R"xml(<GDTF DataVersion="1.1"><FixtureType Name="Odd" Manufacturer="Test">
  <Geometries>
   <Geometry Name="Body">
    <GeometryReference Name="Ghost" Geometry="DoesNotExist"/>
    <Whatever Name="X"/>
   </Geometry>
  </Geometries>
  <DMXModes><DMXMode Name="M" Geometry="Body"><DMXChannels>
   <DMXChannel Offset="1" Geometry="Nowhere"><LogicalChannel Attribute="Dimmer">
    <ChannelFunction Name="D" Attribute="Dimmer" DMXFrom="0/1"/></LogicalChannel></DMXChannel>
   <DMXChannel Offset="2" DMXBreak="2" Geometry="Body"><LogicalChannel Attribute="Dimmer">
    <ChannelFunction Name="D2" Attribute="Dimmer" DMXFrom="0/1"/></LogicalChannel></DMXChannel>
  </DMXChannels></DMXMode></DMXModes>
</FixtureType></GDTF>)xml";
    std::vector<std::string> warnings;
    const FixtureType t = importXml(xml, &warnings, false, false);
    CHECK(anyWarningContains(warnings, "DoesNotExist"));
    CHECK(anyWarningContains(warnings, "Whatever"));
    CHECK(anyWarningContains(warnings, "Nowhere"));
    CHECK(anyWarningContains(warnings, "DMX break"));
    REQUIRE(t.modes.size() == 1);
    CHECK(t.modes[0].channels.size() == 1);          // the break-2 channel was skipped
    CHECK(t.modes[0].channels[0].geometry.empty());  // unknown geometry: whole fixture
    CHECK(t.geometry.children.empty());
}
