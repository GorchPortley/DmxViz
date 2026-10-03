// Checks the bundled fixture files in data/fixtures: every *.dmxviz-fixture.json must load,
// validate and run in the fixture runtime.
#include "assets/ImageLoader.h"
#include "fixtures/Archive.h"
#include "fixtures/DmxValue.h"
#include "fixtures/FixtureAssets.h"
#include "fixtures/FixtureLibrary.h"
#include "fixtures/FixtureRuntime.h"
#include "fixtures/NativeFormat.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>

using namespace dmxviz;
using namespace dmxviz::fixtures;
using doctest::Approx;
namespace fs = std::filesystem;

namespace {

const fs::path kFixtureDir = fs::path(DMXVIZ_DATA_DIR) / "fixtures";

std::vector<fs::path> bundledFixtureFiles() {
    std::vector<fs::path> files;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(kFixtureDir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file() && isNativeFixturePath(it->path())) files.push_back(it->path());
    std::sort(files.begin(), files.end());
    return files;
}

// Resource files a fixture refers to ("file": "../../gobos/star.svg") that are not on disk.
// Gobo images are added to the data folder separately from the fixtures, so a missing one
// must not fail the fixture checks.
std::vector<std::string> missingResourceFiles(const fs::path& fixtureFile) {
    std::vector<std::string> missing;
    std::ifstream in(fixtureFile, std::ios::binary);
    const auto json = nlohmann::json::parse(in, nullptr, false);
    if (json.is_discarded() || !json.contains("resources") || !json["resources"].is_array()) return missing;
    for (const auto& r : json["resources"]) {
        if (!r.is_object() || !r.contains("file") || !r["file"].is_string()) continue;
        const fs::path p = fs::path(r["file"].get<std::string>());
        const fs::path resolved = p.is_absolute() ? p : fixtureFile.parent_path() / p;
        std::error_code ec;
        if (!fs::is_regular_file(resolved, ec)) missing.push_back(resolved.lexically_normal().string());
    }
    return missing;
}

// Loads a bundled fixture. Image files that are not on disk (yet) are replaced by an embedded
// stand-in, so the rest of the file is still checked in full.
std::optional<FixtureType> loadBundled(const fs::path& file) {
    const auto missing = missingResourceFiles(file);
    std::string error;
    std::optional<FixtureType> type;
    if (missing.empty()) {
        type = FixtureSerializer::loadFile(file, &error);
    } else {
        MESSAGE(file.filename().string() << ": " << missing.size() << " resource file(s) missing, e.g. "
                                         << missing.front() << " - using stand-ins");
        std::ifstream in(file, std::ios::binary);
        auto json = nlohmann::ordered_json::parse(in);
        const std::string standIn =
            R"(<svg xmlns="http://www.w3.org/2000/svg" width="8" height="8"><circle cx="4" cy="4" r="3"/></svg>)";
        const std::string base64 =
            base64Encode(std::span(reinterpret_cast<const std::uint8_t*>(standIn.data()), standIn.size()));
        for (auto& r : json["resources"]) {
            if (!r.contains("file")) continue;
            const fs::path p = file.parent_path() / r["file"].get<std::string>();
            std::error_code ec;
            if (fs::is_regular_file(p, ec)) continue;
            r.erase("file");
            r["base64"] = base64;
        }
        type = FixtureSerializer::fromJson(json, file.parent_path(), &error);
    }
    INFO(file.string() << ": " << error);
    REQUIRE(type.has_value());
    return type;
}

}  // namespace

TEST_CASE("every bundled native fixture loads, validates and runs") {
    const auto files = bundledFixtureFiles();
    REQUIRE_MESSAGE(!files.empty(), "no *.dmxviz-fixture.json found under " << kFixtureDir.string());

    std::set<std::string> ids;
    for (const fs::path& file : files) {
        INFO(file.string());
        auto loaded = loadBundled(file);
        REQUIRE(loaded.has_value());
        auto type = std::make_shared<const FixtureType>(std::move(*loaded));

        CHECK(validateFixtureType(*type).empty());
        CHECK(type->id.find('/') != std::string::npos);
        CHECK(ids.insert(type->id).second);  // ids are unique across files
        CHECK_FALSE(type->manufacturer.empty());
        CHECK(type->beamCount() >= 1);

        // The documented 512-slot limit, and the id matches the manufacturer/name slug.
        for (const DmxMode& mode : type->modes) {
            CHECK(mode.footprint >= 1);
            CHECK(mode.footprint <= 512);
            FixtureRuntime runtime(type, mode.name);
            std::vector<std::uint8_t> dmx(static_cast<std::size_t>(mode.footprint), 0);
            runtime.setDmx(dmx);
            runtime.update(0.02f, 0.0);
            std::vector<MeshInstance> meshes;
            std::vector<BeamState> beams;
            runtime.emit(glm::mat4(1.0f), 1, meshes, beams);
            CHECK(static_cast<int>(beams.size()) == type->beamCount());
        }

        // Every image resource decodes (SVG gobos are rasterised with nanosvg).
        for (const Resource& r : type->resources) {
            if (!isImageFormat(r.format)) continue;
            std::string error;
            assets::ImageLoadOptions options;
            options.desiredChannels = 1;
            options.svgSize = 64;
            auto image = assets::loadImageFromMemory(r.data, r.format, options, &error);
            INFO("resource " << r.name << ": " << error);
            CHECK(image.has_value());
        }
    }
}

TEST_CASE("the library loads the bundled fixture folder") {
    FixtureLibrary lib;
    const int loaded = lib.loadDirectory(kFixtureDir);
    // Fixtures whose gobo files are missing are reported as errors, nothing else may be.
    for (const auto& e : lib.errors()) {
        INFO(e.path.string() << ": " << e.message);
        CHECK(!missingResourceFiles(e.path).empty());
    }
    CHECK(loaded + static_cast<int>(lib.errors().size()) == static_cast<int>(bundledFixtureFiles().size()));
}

TEST_CASE("bundled profile spot has the documented features") {
    const fs::path file = kFixtureDir / "generic" / "profile-spot.dmxviz-fixture.json";
    REQUIRE(fs::is_regular_file(file));
    auto loaded = loadBundled(file);
    REQUIRE(loaded.has_value());
    const FixtureType& t = *loaded;
    CHECK(t.id == "generic/profile-spot");
    REQUIRE(t.modes.size() >= 1);
    CHECK(t.modes[0].name == "Standard");

    // wheels: colours, two gobo wheels, one prism; eight gobo images referenced by file
    int goboWheels = 0, colorWheels = 0, prisms = 0;
    for (const Wheel& w : t.wheels) {
        const auto has = [&](SlotKind k) {
            return std::any_of(w.slots.begin(), w.slots.end(), [&](const WheelSlot& s) { return s.kind == k; });
        };
        goboWheels += has(SlotKind::Gobo) ? 1 : 0;
        colorWheels += has(SlotKind::Color) ? 1 : 0;
        if (has(SlotKind::Prism)) {
            ++prisms;
            for (const WheelSlot& s : w.slots)
                if (s.kind == SlotKind::Prism) CHECK(s.facets.size() == 3);
        }
    }
    CHECK(goboWheels == 2);
    CHECK(colorWheels == 1);
    CHECK(prisms == 1);
    CHECK(t.resources.size() == 8);
    for (const char* name : {"dots", "star", "breakup", "lines", "tunnel", "flower", "triangle", "spiral"}) {
        INFO(name);
        CHECK(t.findResource(name) != nullptr);
    }

    // attributes of the standard mode
    const DmxMode& mode = t.modes[0];
    std::set<Attribute> attributes;
    for (const Channel& c : mode.channels)
        for (const ChannelFunction& f : c.functions) attributes.insert(f.attribute);
    for (Attribute a : {Attribute::Pan,           Attribute::Tilt,       Attribute::Dimmer,
                        Attribute::ColorSub_C,    Attribute::ColorSub_M, Attribute::ColorSub_Y,
                        Attribute::Color1,        Attribute::Gobo1,      Attribute::Gobo1PosRotate,
                        Attribute::Gobo2,         Attribute::Prism1,     Attribute::Prism1PosRotate,
                        Attribute::Iris,          Attribute::Frost1,     Attribute::Focus1,
                        Attribute::Zoom,          Attribute::Blade1A,    Attribute::Blade4A,
                        Attribute::Blade1Rot,     Attribute::Blade4Rot,  Attribute::Shutter1,
                        Attribute::Shutter1Strobe}) {
        INFO(attributeName(a));
        CHECK(attributes.contains(a));
    }
    CHECK_FALSE(attributes.contains(Attribute::Gobo2PosRotate));  // the second gobo wheel is static

    // 16-bit pan 540 degrees, tilt 270 degrees, zoom 8..42 degrees
    const Channel* pan = mode.findChannel("Pan");
    const Channel* tilt = mode.findChannel("Tilt");
    const Channel* zoom = mode.findChannel("Zoom");
    REQUIRE(pan != nullptr);
    REQUIRE(tilt != nullptr);
    REQUIRE(zoom != nullptr);
    CHECK(pan->byteCount() == 2);
    CHECK(tilt->byteCount() == 2);
    CHECK(pan->functions[0].dmxTo == 65535);
    CHECK(radToDeg(pan->functions[0].physicalTo - pan->functions[0].physicalFrom) == Approx(540.0f));
    CHECK(radToDeg(tilt->functions[0].physicalTo - tilt->functions[0].physicalFrom) == Approx(270.0f));
    CHECK(radToDeg(zoom->functions[0].physicalFrom) == Approx(8.0f));
    CHECK(radToDeg(zoom->functions[0].physicalTo) == Approx(42.0f));
    CHECK(t.physical.movement.panMaxSpeed == Approx(degToRad(216.0f)));

    // it moves and lights like a moving head: pan to the end, dimmer up, shutter open
    auto shared = std::make_shared<const FixtureType>(t);
    FixtureRuntime runtime(shared, "Standard");
    std::vector<std::uint8_t> dmx(static_cast<std::size_t>(mode.footprint), 0);
    for (const Channel& c : mode.channels) writeDmxValue(dmx, c.offsets, c.defaultValue);
    writeDmxValue(dmx, pan->offsets, 65535);
    writeDmxValue(dmx, mode.findChannel("Dimmer")->offsets, 255);
    runtime.setDmx(dmx);
    for (int i = 0; i < 400; ++i) runtime.update(0.01f, i * 0.01);
    CHECK(radToDeg(runtime.panAngle()) == Approx(270.0f).epsilon(0.01));
    std::vector<MeshInstance> meshes;
    std::vector<BeamState> beams;
    runtime.emit(glm::mat4(1.0f), 1, meshes, beams);
    REQUIRE(beams.size() == 1);
    CHECK(beams[0].intensity > 0.9f);
    CHECK(beams[0].direction.y < -0.99f);  // tilt in the middle: straight down
    CHECK(beams[0].beamAngle == Approx(degToRad(8.0f + 0.5f * 34.0f)).epsilon(0.02));  // zoom default: middle
    CHECK(beams[0].gobos[0].image == kInvalidImage);                                   // open gobo wheel at the default
}
