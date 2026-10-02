#include "fixtures/AttributeEncoder.h"
#include "fixtures/FixtureRuntime.h"
#include "fixtures/OflImporter.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <memory>

using namespace dmxviz;
using namespace dmxviz::fixtures;
using doctest::Approx;
namespace fs = std::filesystem;

namespace {

const fs::path kOflDir = fs::path(DMXVIZ_TEST_DATA_DIR) / "fixtures" / "ofl";

FixtureType importSample(const std::string& file, std::vector<std::string>* warnings = nullptr) {
    std::string error;
    auto t = importOflFile(kOflDir / file, {}, &error, warnings);
    INFO(error);
    REQUIRE(t.has_value());
    CHECK(validateFixtureType(*t).empty());
    return std::move(*t);
}

const Channel& channelNamed(const DmxMode& mode, std::string_view name) {
    const Channel* c = mode.findChannel(name);
    INFO(name);
    REQUIRE(c != nullptr);
    return *c;
}

bool hasFunction(const Channel& c, Attribute a) {
    return std::any_of(c.functions.begin(), c.functions.end(), [&](const ChannelFunction& f) { return f.attribute == a; });
}

// Runtime + encoder + DMX buffer for one mode of an imported type.
struct Patch {
    std::shared_ptr<const FixtureType> type;
    FixtureRuntime runtime;
    AttributeEncoder encoder;
    std::vector<std::uint8_t> dmx;
    Patch(FixtureType t, const std::string& mode)
        : type(std::make_shared<const FixtureType>(std::move(t))),
          runtime(type, mode),
          encoder(*type, *type->findMode(mode)),
          dmx(static_cast<std::size_t>(type->findMode(mode)->footprint), 0) {
        encoder.writeDefaults(dmx);
    }
    std::vector<BeamState> run(float seconds = 0.05f) {
        runtime.setDmx(dmx);
        for (float t = 0; t < seconds; t += 0.01f) runtime.update(0.01f, t);
        std::vector<MeshInstance> meshes;
        std::vector<BeamState> beams;
        runtime.emit(glm::mat4(1.0f), 1, meshes, beams);
        return beams;
    }
};

}  // namespace

TEST_CASE("OFL detection") {
    CHECK(isOflJson(nlohmann::ordered_json::parse(
        R"({"$schema": "https://raw.githubusercontent.com/OpenLightingProject/open-fixture-library/master/schemas/fixture.json"})")));
    CHECK_FALSE(isOflJson(nlohmann::ordered_json::parse(R"({"formatVersion": 1})")));
}

TEST_CASE("OFL spot: Robe Robin 600E Spot") {
    const FixtureType t = importSample("robe/robin-600e-spot.json");
    CHECK(t.id == "robe/robin-600e-spot");
    CHECK(t.manufacturer == "Robe");
    CHECK(t.source == FixtureSource::Ofl);
    REQUIRE(t.modes.size() == 3);
    CHECK(t.modes[0].footprint == 32);
    CHECK(t.modes[1].footprint == 25);
    CHECK(t.modes[2].footprint == 23);
    CHECK(t.modes[0].channels.size() == 23);  // 32 slots, 9 of them fine bytes
    CHECK(t.beamCount() == 1);
    CHECK(t.findGeometry("Yoke")->type == GeometryType::Axis);
    CHECK(t.findGeometry("Beam")->beam.type == BeamType::Spot);

    const DmxMode& full = t.modes[0];
    CHECK(channelNamed(full, "Pan").offsets == std::vector<std::uint16_t>{1, 2});
    CHECK(channelNamed(full, "Pan").geometry == "Yoke");
    const ChannelFunction& pan = channelNamed(full, "Pan").functions.front();
    CHECK(radToDeg(pan.physicalFrom) == Approx(-270.0f));
    CHECK(radToDeg(pan.physicalTo) == Approx(270.0f));
    CHECK(channelNamed(full, "Dimmer").offsets.size() == 2);
    CHECK(channelNamed(t.modes[2], "Pan").offsets.size() == 1);  // 23-channel mode is 8 bit

    // Lamp fixture: OFL "Cyan" ColorIntensity means a CMY flag.
    CHECK(hasFunction(channelNamed(full, "Cyan"), Attribute::ColorSub_C));
    CHECK(hasFunction(channelNamed(full, "CTO Filter"), Attribute::CTO));

    // Wheels: colour, two gobo wheels (Gobo1 static, Gobo2 rotating) and a prism.
    REQUIRE(t.findWheel("Static Gobo Wheel") != nullptr);
    CHECK(t.findWheel("Static Gobo Wheel")->slots.size() == 10);
    CHECK(t.findWheel("Static Gobo Wheel")->slots[1].kind == SlotKind::Gobo);
    CHECK_FALSE(t.findWheel("Static Gobo Wheel")->slots[1].image.empty());  // stand-in image
    CHECK(t.findWheel("Color Wheel")->slots[1].kind == SlotKind::Color);
    CHECK(t.findWheel("Color Wheel")->slots[1].color.r > 0.3f);  // "Deep Red" #aa0000
    CHECK(hasFunction(channelNamed(full, "Static Gobo Wheel"), Attribute::Gobo1));
    CHECK(hasFunction(channelNamed(full, "Rotating Gobo Wheel"), Attribute::Gobo2));
    CHECK(hasFunction(channelNamed(full, "Prism"), Attribute::Prism1));

    // Switching channel: "Rotating Gobo Control" indexes or rotates depending on the gobo wheel channel.
    const Channel& control = channelNamed(full, "Rotating Gobo Control");
    CHECK(control.offsets == std::vector<std::uint16_t>{18, 19});
    CHECK(hasFunction(control, Attribute::Gobo2Pos));
    CHECK(hasFunction(control, Attribute::Gobo2PosRotate));
    for (const ChannelFunction& f : control.functions) CHECK(f.modeMaster == "Rotating Gobo Wheel");

    // Drive it: gobo 3 with indexing, prism in.
    Patch p(t, "25-channel");
    p.encoder.setNormalized(p.dmx, Attribute::Dimmer, 1.0f);
    p.encoder.setShutter(p.dmx, FunctionKind::ShutterOpen);
    CHECK(p.encoder.setWheelSlot(p.dmx, Attribute::Gobo2, 3.0f));
    CHECK(p.encoder.setPhysical(p.dmx, Attribute::Gobo2Pos, degToRad(90.0f)));
    CHECK(p.encoder.setWheelSlot(p.dmx, Attribute::Prism1, 2.0f));
    const auto beams = p.run(2.0f);
    REQUIRE(beams.size() == 1);
    CHECK(beams[0].intensity == Approx(1.0f));
    CHECK(radToDeg(beams[0].gobos[1].rotation) == Approx(90.0f).epsilon(0.02));
    CHECK(beams[0].prism.facetCount == 3);
}

TEST_CASE("OFL spot with gobo resources: Showtec Phantom 50") {
    // Recreate the OFL layout (fixtures/<mfr>/<file>, resources/gobos/<key>.svg) with our own images.
    const fs::path root = fs::temp_directory_path() / "dmxviz-ofl-test";
    fs::remove_all(root);
    fs::create_directories(root / "fixtures" / "showtec");
    fs::create_directories(root / "resources" / "gobos");
    fs::copy_file(kOflDir / "showtec/phantom-50-led-spot.json", root / "fixtures/showtec/phantom-50-led-spot.json");
    const std::string svg =
        R"(<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64"><circle cx="32" cy="32" r="20" fill="#fff"/></svg>)";
    std::ofstream(root / "resources/gobos/3-fold-swirl.svg") << svg;
    std::ofstream(root / "resources/gobos/biohazard.svg") << svg;

    std::vector<std::string> warnings;
    std::string error;
    auto t = importOflFile(root / "fixtures/showtec/phantom-50-led-spot.json", {}, &error, &warnings);
    INFO(error);
    REQUIRE(t.has_value());
    CHECK(t->id == "showtec/phantom-50-led-spot");
    CHECK(t->manufacturer == "Showtec");
    const Resource* swirl = t->findResource("gobo-3-fold-swirl");
    REQUIRE(swirl != nullptr);
    CHECK(swirl->format == "svg");
    CHECK(std::string(swirl->data.begin(), swirl->data.end()) == svg);
    CHECK(t->findResource("gobo-biohazard") != nullptr);
    // Gobos whose images are missing fall back to stand-ins (with a warning each).
    CHECK(warnings.size() >= 5);
    const Wheel* gobos = t->findWheel("Gobo Wheel");
    REQUIRE(gobos != nullptr);
    CHECK(gobos->slots.size() == 8);
    for (std::size_t i = 1; i < gobos->slots.size(); ++i) CHECK(t->findResource(gobos->slots[i].image) != nullptr);
    // 16-bit pan with the fine byte not adjacent to the coarse one.
    CHECK(t->modes[0].findChannel("Pan")->offsets == std::vector<std::uint16_t>{1, 3});
    fs::remove_all(root);
}

TEST_CASE("OFL beam: Clay Paky Sharpy") {
    const FixtureType t = importSample("clay-paky/sharpy.json");
    CHECK(t.findGeometry("Beam")->beam.type == BeamType::Beam);
    CHECK(radToDeg(t.findGeometry("Beam")->beam.beamAngle) == Approx(3.8f));
    CHECK(t.modes.size() == 2);
    CHECK(t.modes[0].footprint == 16);
    CHECK(t.modes[1].footprint == 20);
    CHECK(t.findWheel("Gobo Wheel") != nullptr);
    CHECK(t.findWheel("Color Wheel") != nullptr);
    // Prism insertion + indexed prism rotation by angle.
    CHECK(hasFunction(channelNamed(t.modes[0], "Prism Insertion"), Attribute::Prism1));
    CHECK(hasFunction(channelNamed(t.modes[0], "Prism Rotation"), Attribute::Prism1Pos));
    const Channel& shutter = channelNamed(t.modes[0], "Shutter / Strobe");
    CHECK(hasFunction(shutter, Attribute::Shutter1Strobe));
    CHECK(hasFunction(shutter, Attribute::Shutter1StrobePulse));
}

TEST_CASE("OFL wash with zoom: Martin MAC Viper Wash") {
    const FixtureType t = importSample("martin/mac-viper-wash.json");
    CHECK(t.findGeometry("Beam")->beam.type == BeamType::Wash);
    const DmxMode& basic = t.modes[0];
    CHECK(basic.footprint == 18);
    const ChannelFunction& zoom = channelNamed(basic, "Zoom").functions.front();
    CHECK(zoom.attribute == Attribute::Zoom);
    CHECK(radToDeg(zoom.physicalFrom) == Approx(59.0f));  // "wide" = lens maximum
    CHECK(radToDeg(zoom.physicalTo) < 15.0f);              // "narrow"
    CHECK(hasFunction(channelNamed(basic, "Internal barndoor 1"), Attribute::Blade1A));
    CHECK(hasFunction(channelNamed(basic, "Internal barndoors rotation"), Attribute::ShaperRot));
    CHECK(hasFunction(channelNamed(basic, "Magenta"), Attribute::ColorSub_M));

    Patch p(t, "Basic");
    p.encoder.setNormalized(p.dmx, Attribute::Dimmer, 1.0f);
    p.encoder.setShutter(p.dmx, FunctionKind::ShutterOpen);
    CHECK(p.encoder.setPhysical(p.dmx, Attribute::Zoom, degToRad(30.0f)));
    CHECK(p.encoder.setColor(p.dmx, {1.0f, 0.0f, 0.0f}));  // CMY: magenta + yellow
    const auto beams = p.run();
    CHECK(radToDeg(beams[0].beamAngle) == Approx(30.0f).epsilon(0.02));
    CHECK(beams[0].color.r == Approx(1.0f));
    CHECK(beams[0].color.g < 0.01f);
    CHECK(beams[0].color.b < 0.01f);
}

TEST_CASE("OFL RGBW par: Showtec Club Par 12/4") {
    const FixtureType t = importSample("showtec/club-par-12-4-rgbw.json");
    REQUIRE(t.modes.size() == 2);
    CHECK(t.modes[0].footprint == 4);
    CHECK(t.modes[1].footprint == 9);
    CHECK(t.beamCount() == 1);
    CHECK(hasFunction(channelNamed(t.modes[0], "White"), Attribute::ColorAdd_W));

    // 4-channel mode has no dimmer: intensity comes from the LED levels.
    Patch p(t, "RGBW");
    p.dmx = {255, 0, 0, 0};
    auto beams = p.run();
    CHECK(beams[0].color.r == Approx(1.0f));
    CHECK(beams[0].intensity == Approx(0.5f).epsilon(0.01));

    // 9-channel mode: strobe channel at 0 means "no strobe" (OFL encodes it as Closed).
    Patch q(t, "DRGBW Strobe & Program");
    q.dmx = {255, 255, 255, 255, 255, 0, 0, 0, 0};
    beams = q.run();
    CHECK(beams[0].intensity == Approx(1.0f));
}

TEST_CASE("OFL pixel bar matrix: Showtec Pixel Bar 12 MKII") {
    const FixtureType t = importSample("showtec/pixel-bar-12-mkii.json");
    CHECK(t.beamCount() == 12);
    CHECK(t.geometryGroups.size() == 15);
    REQUIRE(t.findMode("RGB Individual") != nullptr);
    const DmxMode& individual = *t.findMode("RGB Individual");
    CHECK(individual.footprint == 36);
    CHECK(individual.channels.front().name == "Red 1");
    CHECK(individual.channels.front().geometry == "Pixel 1");
    const DmxMode& halves = *t.findMode("RGB Halves");
    CHECK(halves.footprint == 6);
    CHECK(halves.channels[0].geometry == "Pixel group 1/2");
    CHECK(t.findMode("RGB")->channels[0].geometry.empty());  // "Master" = all pixels = whole fixture

    // Pixel keys run 12..1 left to right, "1/2" is x >= 7, i.e. the right half.
    Patch p(t, "RGB Halves");
    p.dmx = {255, 0, 0, 0, 0, 255};
    const auto beams = p.run();
    REQUIRE(beams.size() == 12);
    for (const BeamState& b : beams) {
        if (b.position.x > 0.0f) {
            CHECK(b.color.r == Approx(1.0f));
            CHECK(b.color.b == Approx(0.0f));
        } else {
            CHECK(b.color.b == Approx(1.0f));
            CHECK(b.color.r == Approx(0.0f));
        }
        CHECK(b.intensity == Approx(1.0f));
    }
}

TEST_CASE("OFL strobe: Martin Atomic 3000") {
    const FixtureType t = importSample("martin/atomic-3000.json");
    CHECK(t.findGeometry("Beam")->beam.type == BeamType::Rectangle);
    REQUIRE(t.modes.size() == 3);
    CHECK(hasFunction(channelNamed(t.modes[1], "Flash Rate"), Attribute::StrobeFrequency));
    CHECK(hasFunction(channelNamed(t.modes[1], "Flash Duration"), Attribute::StrobeDuration));

    // 3-channel mode has no shutter channel: a flash rate > 0 strobes.
    Patch p(t, "3-channel");
    p.dmx = {255, 5, 128};
    p.runtime.setDmx(p.dmx);
    int on = 0, off = 0;
    std::vector<MeshInstance> meshes;
    std::vector<BeamState> beams;
    for (int i = 0; i < 1000; ++i) {
        p.runtime.update(0.001f, i * 0.001);
        beams.clear();
        p.runtime.emit(glm::mat4(1.0f), 1, meshes, beams);
        (beams[0].intensity > 0.5f ? on : off)++;
    }
    CHECK(on > 0);
    CHECK(off > on);  // short xenon-style flashes
    // Rate 0..5 is "single flash": steady light.
    p.dmx = {255, 20, 0};
    CHECK(p.run()[0].intensity == Approx(1.0f));
}
