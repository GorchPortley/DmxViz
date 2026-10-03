#include "FixtureTestGdtf.h"
#include "FixtureTestUtil.h"
#include "fixtures/Archive.h"
#include "fixtures/GdtfImporter.h"
#include "fixtures/NativeFormat.h"
#include "fixtures/OflImporter.h"

#include <doctest/doctest.h>
#include <glm/gtx/euler_angles.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using namespace dmxviz;
using namespace dmxviz::fixtures;
using namespace dmxviz::fixtures::test;
using doctest::Approx;
namespace fs = std::filesystem;

namespace {

// Writes, reads and writes again: a faithful round trip gives identical text the second time.
FixtureType roundTrip(const FixtureType& original) {
    const std::string first = FixtureSerializer::toString(original);
    std::string error;
    auto loaded = FixtureSerializer::fromString(first, {}, &error);
    INFO(error);
    REQUIRE(loaded.has_value());
    CHECK(FixtureSerializer::toString(*loaded) == first);
    CHECK(validateFixtureType(*loaded).empty());
    return std::move(*loaded);
}

std::string errorOf(const std::string& json) {
    std::string error;
    auto t = FixtureSerializer::fromString(json, {}, &error);
    CHECK_FALSE(t.has_value());
    return error;
}

// A minimal valid file; tests patch single fields of it.
nlohmann::ordered_json minimalJson() {
    return nlohmann::ordered_json::parse(R"({
        "formatVersion": 1, "id": "t/min", "name": "Min",
        "geometry": {"name": "Body", "type": "beam", "model": {"primitive": "box", "size": [0.1, 0.1, 0.1]}, "beam": {}},
        "modes": [{"name": "1ch", "channels": [
            {"name": "Dimmer", "offsets": [1], "functions": [{"attribute": "Dimmer"}]}]}]
    })");
}

bool sameRotation(const glm::quat& a, const glm::quat& b) {
    return std::abs(glm::dot(a, b)) > 0.99999f;
}

struct TempDir {
    fs::path path = fs::temp_directory_path() /
                    ("dmxviz_native_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(&path) & 0xFFFFFF));
    TempDir() {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

}  // namespace

TEST_CASE("native format round trip: moving head with wheels, 16-bit channels and resources") {
    FixtureType spot = makeTestSpot();
    spot.shortName = "TS";
    spot.description = "Round trip test";
    spot.revision = "7";
    spot.categories = {"Moving Head"};
    spot.physical.weight = 18.5f;
    spot.physical.power = 700.0f;
    spot.physical.dimensions = {0.3f, 0.5f, 0.25f};
    spot.physical.dimmerCurve = DimmerCurve::SquareLaw;
    spot.physical.movement.panMaxSpeed = degToRad(300.0f);
    spot.physical.movement.tiltAcceleration = degToRad(450.0f);
    // Rotation, position, mesh and colour of a part; a highlight value; a function with sets.
    Geometry* head = spot.findGeometry("Head");
    REQUIRE(head != nullptr);
    head->rotation = glm::quat_cast(glm::eulerAngleXYZ(degToRad(20.0f), degToRad(-35.0f), degToRad(10.0f)));
    head->position = {0.01f, -0.25f, 0.02f};
    head->model.color = {0.2f, 0.1f, 0.05f};
    spot.modes[0].channels[2].highlightValue = 255;
    spot.modes[0].channels[0].functions[0].sets.push_back({"Left", 0, 32767});
    spot.modes[0].channels[0].functions[0].sets.push_back({"Right", 32768, 65535});

    const FixtureType loaded = roundTrip(spot);
    CHECK(loaded.id == "test/spot");
    CHECK(loaded.shortName == "TS");
    CHECK(loaded.description == "Round trip test");
    CHECK(loaded.revision == "7");
    CHECK(loaded.categories == std::vector<std::string>{"Moving Head"});
    CHECK(loaded.source == FixtureSource::Native);
    CHECK(loaded.physical.weight == Approx(18.5f));
    CHECK(loaded.physical.dimensions.y == Approx(0.5f));
    CHECK(loaded.physical.dimmerCurve == DimmerCurve::SquareLaw);
    CHECK(loaded.physical.movement.panMaxSpeed == Approx(degToRad(300.0f)).epsilon(1e-4));
    CHECK(loaded.physical.movement.tiltAcceleration == Approx(degToRad(450.0f)).epsilon(1e-4));

    // geometry
    const Geometry* h = loaded.findGeometry("Head");
    REQUIRE(h != nullptr);
    CHECK(h->type == GeometryType::Axis);
    CHECK(sameRotation(h->rotation, head->rotation));
    CHECK(h->position.y == Approx(-0.25f));
    CHECK(h->model.color.r == Approx(0.2f));
    CHECK(loaded.beamCount() == 1);
    const Geometry* beam = loaded.findGeometry("Beam");
    REQUIRE(beam != nullptr);
    CHECK(beam->beam.type == BeamType::Spot);
    CHECK(beam->beam.beamAngle == Approx(degToRad(20.0f)).epsilon(1e-4));
    CHECK(beam->beam.fieldAngle == Approx(degToRad(24.0f)).epsilon(1e-4));

    // wheels and resources
    REQUIRE(loaded.wheels.size() == 3);
    CHECK(loaded.wheels[0].slots.size() == 4);
    CHECK(loaded.wheels[0].slots[1].color.r == Approx(1.0f));
    CHECK(loaded.wheels[1].slots[1].kind == SlotKind::Gobo);
    CHECK(loaded.wheels[1].slots[1].image == "dots");
    REQUIRE(loaded.wheels[2].slots[1].facets.size() == 3);
    CHECK(loaded.wheels[2].slots[1].facets[0].y == Approx(degToRad(5.0f)).epsilon(1e-3));
    REQUIRE(loaded.resources.size() == 1);
    CHECK(loaded.resources[0].name == "dots");
    CHECK(loaded.resources[0].format == "svg");
    CHECK(loaded.resources[0].data == spot.resources[0].data);

    // 16-bit channels keep their offsets, defaults and ranges
    const DmxMode& mode = loaded.modes[0];
    CHECK(mode.footprint == 17);
    const Channel* pan = mode.findChannel("Pan");
    REQUIRE(pan != nullptr);
    CHECK(pan->offsets == std::vector<std::uint16_t>{1, 2});
    CHECK(pan->geometry == "Yoke");
    CHECK(pan->defaultValue == 32768);
    REQUIRE(pan->functions.size() == 1);
    CHECK(pan->functions[0].dmxTo == 65535);
    CHECK(pan->functions[0].physicalFrom == Approx(degToRad(-270.0f)).epsilon(1e-4));
    REQUIRE(pan->functions[0].sets.size() == 2);
    CHECK(pan->functions[0].sets[1].name == "Right");
    CHECK(pan->functions[0].sets[1].dmxFrom == 32768);
    CHECK(mode.findChannel("Dimmer")->highlightValue == 255);

    // wheel functions keep wheel, slot and kind; unusual kinds are written explicitly
    const Channel* color = mode.findChannel("Color");
    REQUIRE(color != nullptr);
    CHECK(color->functions[1].kind == FunctionKind::WheelSlot);
    CHECK(color->functions[1].wheel == "Colors");
    CHECK(color->functions[1].slotFrom == Approx(2.0f));
    const Channel* shutter = mode.findChannel("Shutter");
    REQUIRE(shutter != nullptr);
    CHECK(shutter->functions[0].kind == FunctionKind::ShutterClosed);
    CHECK(shutter->functions[3].attribute == Attribute::Shutter1StrobeRandom);
    CHECK(shutter->functions[3].kind == FunctionKind::StrobeRandom);
}

TEST_CASE("native format keeps rotations, including the gimbal-lock cases") {
    const glm::vec3 angles[] = {{0, 0, 0},       {90, 0, 0},  {0, 90, 0},  {0, -90, 0},   {30, 90, 45},
                                {-120, 45, 170}, {180, 0, 0}, {0, 0, 135}, {20, -35, 10}, {75, -89.99f, 12}};
    for (const glm::vec3& deg : angles) {
        INFO("rotation " << deg.x << " " << deg.y << " " << deg.z);
        FixtureType t = makeTestRgbwPar();
        Geometry* body = t.findGeometry("Body");
        REQUIRE(body != nullptr);
        body->rotation = glm::quat_cast(glm::eulerAngleXYZ(degToRad(deg.x), degToRad(deg.y), degToRad(deg.z)));
        // Just next to the lock (not on it) x and z are ill-conditioned: the rotation survives,
        // but the last digits of the angles may differ from one write to the next.
        const float distanceToLock = std::abs(std::abs(deg.y) - 90.0f);
        const bool nearLock = distanceToLock > 1e-3f && distanceToLock < 1.0f;
        std::string error;
        auto loaded = FixtureSerializer::fromString(FixtureSerializer::toString(t), {}, &error);
        INFO(error);
        REQUIRE(loaded.has_value());
        CHECK(sameRotation(loaded->findGeometry("Body")->rotation, body->rotation));
        if (!nearLock) roundTrip(t);
    }
}

TEST_CASE("native format round trip: multi-cell fixture with groups, geometry root and mode master") {
    FixtureType bar = makeTestPixelBar(8);
    bar.geometryGroups.push_back(
        {"Left half",
         {pixelGeometryName("1"), pixelGeometryName("2"), pixelGeometryName("3"), pixelGeometryName("4")}});
    DmxMode& mode = bar.modes[0];
    mode.description = "8 cells";
    mode.geometryRoot = geometry_names::kBody;
    // a macro channel that only counts while "Dimmer" is above half
    mode.channels.push_back(channel("Macro", {26}, "Left half", {fn(Attribute::Effects1, 0, 255, 0, 1)}));
    mode.channels.back().functions[0].modeMaster = "Dimmer";
    mode.channels.back().functions[0].modeFrom = 128;
    mode.channels.back().functions[0].modeTo = 255;
    mode.footprint = 26;

    const FixtureType loaded = roundTrip(bar);
    CHECK(loaded.beamCount() == 8);
    REQUIRE(loaded.geometryGroups.size() == 1);
    CHECK(loaded.geometryGroups[0].members.size() == 4);
    const DmxMode& m = loaded.modes[0];
    CHECK(m.channels.size() == 26);
    CHECK(m.geometryRoot == "Body");
    CHECK(m.description == "8 cells");
    const Channel* blue8 = m.findChannel("Blue 8");
    REQUIRE(blue8 != nullptr);
    CHECK(blue8->offsets == std::vector<std::uint16_t>{25});
    CHECK(blue8->geometry == pixelGeometryName("8"));
    const Channel* macro = m.findChannel("Macro");
    REQUIRE(macro != nullptr);
    CHECK(macro->geometry == "Left half");
    CHECK(macro->functions[0].modeMaster == "Dimmer");
    CHECK(macro->functions[0].modeFrom == 128);
    CHECK(macro->functions[0].modeTo == 255);
}

TEST_CASE("native format round trip: imported GDTF and OFL fixtures") {
    std::string error;
    auto gdtf = importGdtf(makeGdtfArchive(movingHeadXml()), &error);
    INFO(error);
    REQUIRE(gdtf.has_value());
    const FixtureType loadedGdtf = roundTrip(*gdtf);
    CHECK(loadedGdtf.source == FixtureSource::Gdtf);
    CHECK(loadedGdtf.resources.size() == gdtf->resources.size());  // gobo image and 3DS model
    CHECK(loadedGdtf.geometry.model.mesh == gdtf->geometry.model.mesh);
    CHECK(loadedGdtf.modes[0].findChannel("Body_Effects1")->functions[0].modeMaster == "Body_Control");
    // An attribute that is not in the vocabulary survives with its original name.
    const Channel* custom = loadedGdtf.modes[0].findChannel("Body_MyCustomAttr");
    REQUIRE(custom != nullptr);
    CHECK(custom->functions[0].attribute == Attribute::Unknown);
    CHECK(custom->functions[0].attributeName == "MyCustomAttr");
    CHECK(loadedGdtf.physical.movement.panMaxSpeed == Approx(gdtf->physical.movement.panMaxSpeed).epsilon(1e-4));

    const fs::path oflDir = fs::path(DMXVIZ_TEST_DATA_DIR) / "fixtures" / "ofl";
    for (const char* file : {"clay-paky/sharpy.json", "showtec/pixel-bar-12-mkii.json", "martin/atomic-3000.json"}) {
        auto ofl = importOflFile(oflDir / file, {}, &error);
        INFO(file << ": " << error);
        REQUIRE(ofl.has_value());
        const FixtureType loaded = roundTrip(*ofl);
        CHECK(loaded.source == FixtureSource::Ofl);
        CHECK(loaded.beamCount() == ofl->beamCount());
        CHECK(loaded.modes.size() == ofl->modes.size());
    }
}

TEST_CASE("native format files: embedded and external resources") {
    TempDir dir;
    const FixtureType spot = makeTestSpot();
    std::string error;

    // Embedded (default): one self-contained file.
    const fs::path embedded = dir.path / "embedded.dmxviz-fixture.json";
    REQUIRE(FixtureSerializer::saveFile(spot, embedded, &error));
    auto fromEmbedded = FixtureSerializer::loadFile(embedded, &error);
    INFO(error);
    REQUIRE(fromEmbedded.has_value());
    CHECK(fromEmbedded->resources[0].data == spot.resources[0].data);

    // External: resources go into "<stem>.resources/" and are referenced relative to the file.
    NativeSaveOptions options;
    options.embedResources = false;
    const fs::path external = dir.path / "sub" / "external.dmxviz-fixture.json";
    fs::create_directories(external.parent_path());
    REQUIRE(FixtureSerializer::saveFile(spot, external, &error, options));
    CHECK(fs::is_regular_file(external.parent_path() / "external.resources" / "dots.svg"));
    const auto json = nlohmann::ordered_json::parse(std::ifstream(external));
    REQUIRE(json["resources"].size() == 1);
    CHECK(json["resources"][0]["file"] == "external.resources/dots.svg");
    CHECK_FALSE(json["resources"][0].contains("base64"));
    auto fromExternal = FixtureSerializer::loadFile(external, &error);
    INFO(error);
    REQUIRE(fromExternal.has_value());
    CHECK(fromExternal->resources[0].data == spot.resources[0].data);

    // Relative paths go up and down the tree (the starter fixtures use "../../gobos/x.svg").
    fs::create_directories(dir.path / "gobos");
    fs::create_directories(dir.path / "fixtures" / "generic");
    writeFileBytes(dir.path / "gobos" / "dot.svg", spot.resources[0].data);
    auto j = minimalJson();
    j["resources"] =
        nlohmann::ordered_json::array({{{"name", "dot"}, {"format", "svg"}, {"file", "../../gobos/dot.svg"}}});
    j["wheels"] =
        nlohmann::ordered_json::parse(R"([{"name": "G", "slots": [{"type": "gobo", "name": "Dot", "image": "dot"}]}])");
    const fs::path relative = dir.path / "fixtures" / "generic" / "rel.dmxviz-fixture.json";
    std::ofstream(relative) << j.dump(2);
    auto fromRelative = FixtureSerializer::loadFile(relative, &error);
    INFO(error);
    REQUIRE(fromRelative.has_value());
    CHECK(fromRelative->resources[0].data == spot.resources[0].data);

    // A missing file is an error that names the field and the path.
    j["resources"][0]["file"] = "../../gobos/missing.svg";
    std::ofstream(relative) << j.dump(2);
    CHECK_FALSE(FixtureSerializer::loadFile(relative, &error).has_value());
    CHECK(error.find("resources[0].file") != std::string::npos);
    CHECK(error.find("missing.svg") != std::string::npos);

    // A file that is not there at all.
    CHECK_FALSE(FixtureSerializer::loadFile(dir.path / "nope.dmxviz-fixture.json", &error).has_value());
    CHECK(error.find("cannot open") != std::string::npos);
    CHECK(isNativeFixturePath("a/b/c.dmxviz-fixture.json"));
    CHECK_FALSE(isNativeFixturePath("a/b/c.json"));
}

TEST_CASE("native format reader: hand-written conveniences") {
    auto j = minimalJson();
    // colours as "#rrggbb" (sRGB) or linear arrays, facetCount shorthand, single function without a range
    j["wheels"] = nlohmann::ordered_json::parse(R"([
        {"name": "Colors", "slots": [
            {"type": "open", "name": "Open"},
            {"type": "color", "name": "Red", "color": "#ff0000"},
            {"type": "color", "name": "Half", "color": [0.5, 0.5, 0.5]}]},
        {"name": "Prism", "slots": [{"type": "open"}, {"type": "prism", "facetCount": 4, "deflection": 7}]}
    ])");
    j["emitters"] = nlohmann::ordered_json::parse(R"([{"name": "Deep Red", "color": "#ff2000", "wavelength": 640}])");
    j["modes"][0]["channels"][0]["functions"][0]["physical"] = nlohmann::ordered_json::array({0, 100});
    std::string error;
    auto t = FixtureSerializer::fromJson(j, {}, &error);
    INFO(error);
    REQUIRE(t.has_value());
    CHECK(t->wheels[0].slots[1].color.r == Approx(1.0f));
    CHECK(t->wheels[0].slots[1].color.g == Approx(0.0f));
    CHECK(t->wheels[0].slots[2].color.g == Approx(0.5f));
    REQUIRE(t->wheels[1].slots[1].facets.size() == 4);
    CHECK(glm::length(t->wheels[1].slots[1].facets[0]) == Approx(degToRad(7.0f)).epsilon(1e-4));
    CHECK(t->emitters[0].dominantWavelength == Approx(640.0f));
    CHECK(t->emitters[0].color.r == Approx(1.0f));
    const Channel& dimmer = t->modes[0].channels[0];
    CHECK(dimmer.functions[0].dmxFrom == 0);
    CHECK(dimmer.functions[0].dmxTo == 255);  // whole channel when the only function has no range
    CHECK(dimmer.functions[0].physicalTo == Approx(100.0f));
    CHECK(t->modes[0].footprint == 1);  // footprint defaults to the highest offset
    CHECK(t->source == FixtureSource::Native);

    // Angles in files are degrees: a pan function of +-270 is +-4.71 rad in memory.
    j["modes"][0]["channels"].push_back(nlohmann::ordered_json::parse(
        R"({"name": "Pan", "offsets": [2, 3], "functions": [{"attribute": "Pan", "physical": [-270, 270]}]})"));
    auto withPan = FixtureSerializer::fromJson(j, {}, &error);
    INFO(error);
    REQUIRE(withPan.has_value());
    const Channel& pan = withPan->modes[0].channels[1];
    CHECK(pan.functions[0].dmxTo == 65535);  // 16-bit: range defaults to the channel maximum
    CHECK(pan.functions[0].physicalFrom == Approx(degToRad(-270.0f)));
    CHECK(withPan->modes[0].footprint == 3);

    // The ordered and the plain nlohmann reader behave the same.
    auto plain = FixtureSerializer::fromJson(nlohmann::json::parse(j.dump()), {}, &error);
    REQUIRE(plain.has_value());
    CHECK(FixtureSerializer::toString(*plain) == FixtureSerializer::toString(*withPan));
}

TEST_CASE("native format reader: errors name the JSON path") {
    auto patched = [](auto&& edit) {
        auto j = minimalJson();
        edit(j);
        return j.dump();
    };
    auto contains = [](const std::string& error, std::string_view text) {
        return error.find(text) != std::string::npos;
    };

    CHECK(contains(errorOf("{ nope"), "invalid JSON"));
    CHECK(contains(errorOf("[]"), "expected an object"));
    CHECK(contains(errorOf(patched([](auto& j) { j.erase("formatVersion"); })), "formatVersion"));
    CHECK(contains(errorOf(patched([](auto& j) { j["formatVersion"] = 99; })), "unsupported version 99"));
    CHECK(contains(errorOf(patched([](auto& j) { j.erase("id"); })), "id: required string"));
    CHECK(contains(errorOf(patched([](auto& j) { j["name"] = 5; })), "name: expected a string"));
    CHECK(contains(errorOf(patched([](auto& j) { j["source"] = "dmx"; })), "source"));
    CHECK(contains(errorOf(patched([](auto& j) { j.erase("geometry"); })), "geometry: required object"));
    CHECK(contains(errorOf(patched([](auto& j) { j["geometry"]["type"] = "wheel"; })), "geometry.type"));
    CHECK(contains(errorOf(patched([](auto& j) { j["geometry"]["model"]["primitive"] = "teapot"; })),
                   "geometry.model.primitive"));
    CHECK(contains(errorOf(patched([](auto& j) { j["geometry"]["beam"]["beamAngle"] = 200; })),
                   "geometry.beam.beamAngle"));
    CHECK(contains(errorOf(patched([](auto& j) { j["geometry"]["position"] = nlohmann::json::array({1, 2}); })),
                   "geometry.position: expected an array of 3 numbers"));
    CHECK(contains(errorOf(patched([](auto& j) { j.erase("modes"); })), "modes: required array"));
    CHECK(contains(errorOf(patched([](auto& j) { j["modes"][0]["channels"][0].erase("offsets"); })),
                   "modes[0].channels[0].offsets"));
    CHECK(contains(
        errorOf(patched([](auto& j) { j["modes"][0]["channels"][0]["offsets"] = nlohmann::json::array({0}); })),
        "modes[0].channels[0].offsets[0]: offsets are 1..512"));
    CHECK(contains(errorOf(patched(
                       [](auto& j) { j["modes"][0]["channels"][0]["offsets"] = nlohmann::json::array({1, 2, 3, 4}); })),
                   "0..3 offsets"));
    CHECK(
        contains(errorOf(patched([](auto& j) { j["modes"][0]["channels"][0]["functions"] = nlohmann::json::array(); })),
                 "at least one function"));
    CHECK(contains(errorOf(patched([](auto& j) {
                       j["modes"][0]["channels"][0]["functions"][0]["dmx"] = nlohmann::json::array({0});
                   })),
                   "modes[0].channels[0].functions[0].dmx: expected [from, to]"));
    CHECK(contains(errorOf(patched([](auto& j) {
                       j["modes"][0]["channels"][0]["functions"][0]["dmx"] = nlohmann::json::array({0, 999});
                   })),
                   "range [0, 999] is invalid for a 8-bit channel"));
    CHECK(contains(errorOf(patched([](auto& j) { j["modes"][0]["channels"][0]["functions"][0]["kind"] = "wobble"; })),
                   "unknown kind"));
    CHECK(
        contains(errorOf(patched([](auto& j) { j["modes"][0]["channels"][0]["functions"][0]["kind"] = "wheelSlot"; })),
                 "wheel functions need"));
    CHECK(contains(
        errorOf(patched([](auto& j) { j["modes"][0]["channels"][0]["functions"][0]["modeMaster"] = "Dimmer"; })),
        "modeRange"));
    CHECK(contains(errorOf(patched([](auto& j) { j["modes"][0]["channels"][0]["geometry"] = "Nowhere"; })),
                   "unknown geometry or group \"Nowhere\""));
    CHECK(contains(errorOf(patched([](auto& j) {
                       j["wheels"] = nlohmann::json::parse(R"([{"name": "W", "slots": [{"type": "marble"}]}])");
                   })),
                   "wheels[0].slots[0].type"));
    CHECK(contains(errorOf(patched([](auto& j) {
                       j["wheels"] =
                           nlohmann::json::parse(R"([{"name": "W", "slots": [{"type": "gobo", "name": "g"}]}])");
                   })),
                   "need an image resource"));
    CHECK(contains(errorOf(patched([](auto& j) {
                       j["wheels"] = nlohmann::json::parse(R"([{"name": "W", "slots": [{"type": "prism"}]}])");
                   })),
                   "facets"));
    CHECK(contains(errorOf(patched([](auto& j) {
                       j["resources"] = nlohmann::json::parse(R"([{"name": "r", "format": "png", "base64": "!!!"}])");
                   })),
                   "resources[0].base64: invalid base64"));
    CHECK(contains(errorOf(patched(
                       [](auto& j) { j["resources"] = nlohmann::json::parse(R"([{"name": "r", "format": "png"}])"); })),
                   "needs \"base64\" or \"file\""));
    CHECK(contains(errorOf(patched(
                       [](auto& j) { j["physical"] = nlohmann::json::parse(R"({"movement": {"panMaxSpeed": -1}})"); })),
                   "must be positive"));

    // several problems: the first is reported and the count of the others is mentioned
    const std::string many = errorOf(patched([](auto& j) {
        j["modes"][0]["channels"][0]["geometry"] = "A";
        j["geometry"]["children"] = nlohmann::json::parse(R"([{"name": "Body"}])");
    }));
    CHECK(contains(many, "more problem"));
}

TEST_CASE("native format: the complete example in docs/FIXTURE_FORMAT.md loads") {
    const fs::path doc = fs::path(DMXVIZ_DATA_DIR) / ".." / "docs" / "FIXTURE_FORMAT.md";
    std::ifstream in(doc, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "cannot open " << doc.string());
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::erase(text, '\r');  // tolerate CRLF checkouts

    const std::size_t heading = text.find("## Complete example");
    REQUIRE(heading != std::string::npos);
    const std::size_t open = text.find("```json\n", heading);
    REQUIRE(open != std::string::npos);
    const std::size_t begin = open + 8;
    const std::size_t end = text.find("\n```", begin);
    REQUIRE(end != std::string::npos);

    std::string error;
    auto type = FixtureSerializer::fromString(text.substr(begin, end - begin), doc.parent_path(), &error);
    INFO(error);
    REQUIRE(type.has_value());
    CHECK(type->id == "acme/example-spot");
    CHECK(type->modes.size() == 2);
    CHECK(type->modes[0].channels.size() == 13);  // 15 DMX slots: pan and tilt use two each
    CHECK(type->modes[0].footprint == 15);
    CHECK(type->modes[1].footprint == 6);
    CHECK(type->beamCount() == 1);
    CHECK(type->resources.size() == 1);
    CHECK(type->emitters.size() == 1);
    CHECK(type->wheels.size() == 2);
    CHECK(type->wheels[1].slots[1].facets.size() == 3);
    CHECK(type->modes[0].findChannel("Pan")->offsets.size() == 2);
    CHECK(type->modes[0].findChannel("Effect")->functions[0].modeMaster == "Control");
    CHECK(type->modes[0].findChannel("Dimmer")->geometry == "Lens");
    roundTrip(*type);
}
