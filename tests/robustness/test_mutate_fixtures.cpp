// Mutation robustness of the fixture loaders: native JSON, Open Fixture Library JSON and GDTF
// (the zip container and description.xml). Whatever a loader accepts is also run through the
// encoder and the runtime, because that is what a damaged library file would reach in the app.

#include "../fixtures/FixtureTestGdtf.h"
#include "Mutate.h"

#include "fixtures/Archive.h"
#include "fixtures/AttributeEncoder.h"
#include "fixtures/FixtureRuntime.h"
#include "fixtures/GdtfImporter.h"
#include "fixtures/NativeFormat.h"
#include "fixtures/OflImporter.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

using namespace dmxviz;
using namespace dmxviz::fixtures;
using namespace dmxviz::robust;
namespace fs = std::filesystem;

namespace {

std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::vector<fs::path> filesWithExtension(const fs::path& dir, const std::string& extension) {
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(dir))
        if (entry.is_regular_file() && entry.path().extension() == extension) files.push_back(entry.path());
    std::sort(files.begin(), files.end());  // directory order is not deterministic
    return files;
}

// What the app does with a fixture type it loaded: validate it, spawn a runtime for each mode,
// feed it DMX and emit. Must never crash, whatever the (accepted) type contains.
void exercise(const FixtureType& type, Rng& rng) {
    static_cast<void>(validateFixtureType(type));
    const auto shared = std::make_shared<const FixtureType>(type);
    std::size_t modes = 0;
    for (const DmxMode& mode : shared->modes) {
        if (++modes > 3) break;
        FixtureRuntime runtime(shared, mode.name);
        AttributeEncoder encoder(*shared, mode);
        std::vector<std::uint8_t> dmx(static_cast<std::size_t>(std::clamp(runtime.footprint(), 0, 4096)), 0);
        encoder.writeDefaults(dmx);
        for (int frame = 0; frame < 3; ++frame) {
            for (std::uint8_t& b : dmx) b = rng.chance(3) ? static_cast<std::uint8_t>(rng.next()) : b;
            runtime.setDmx(dmx);
            runtime.update(0.016f, 0.016 * frame);
        }
        std::vector<MeshInstance> meshes;
        std::vector<BeamState> beams;
        runtime.emit(glm::mat4(1.0f), 1, meshes, beams);
        static_cast<void>(runtime.values());
        encoder.writeHighlight(dmx);
    }
}

}  // namespace

TEST_CASE("mutation: native fixture JSON") {
    Rng rng(0x4A71FE01);
    std::vector<std::string> samples;
    std::vector<fs::path> baseDirs;  // "file" resources (gobos, models) are relative to the fixture file
    for (const fs::path& file : filesWithExtension(fs::path(DMXVIZ_DATA_DIR) / "fixtures" / "generic", ".json")) {
        samples.push_back(readText(file));
        baseDirs.push_back(file.parent_path());
    }
    // A richer one: an imported GDTF moving head written in the native format.
    std::string error;
    if (auto imported = importGdtf(test::makeGdtfArchive(test::movingHeadXml()), &error)) {
        samples.push_back(FixtureSerializer::toString(*imported));
        baseDirs.emplace_back();
    }
    REQUIRE(samples.size() >= 8);

    std::vector<nlohmann::json> documents;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        documents.push_back(nlohmann::json::parse(samples[i]));
        auto type = FixtureSerializer::fromString(samples[i], baseDirs[i], &error);
        REQUIRE_MESSAGE(type, error);
        exercise(*type, rng);
    }

    SlowestCall slowest;
    std::size_t accepted = 0;
    for (std::size_t i = 0; i < 1500; ++i) {
        const std::size_t which = i % samples.size();
        const std::string text = mutateJsonText(documents[which], samples[which], rng, i);
        slowest.run([&] {
            if (auto type = FixtureSerializer::fromString(text, baseDirs[which], &error)) {
                ++accepted;
                exercise(*type, rng);
            }
        });
    }
    MESSAGE("native: " << accepted << " of 1500 mutations were still accepted");
    CHECK(accepted > 0);  // some edits are harmless; the loop is not only testing rejection
    CHECK(slowest.seconds() < 2.0);
}

TEST_CASE("mutation: Open Fixture Library JSON") {
    Rng rng(0x0F11B002);
    std::vector<std::string> samples;
    for (const fs::path& file : filesWithExtension(fs::path(DMXVIZ_TEST_DATA_DIR) / "fixtures" / "ofl", ".json"))
        samples.push_back(readText(file));
    REQUIRE(samples.size() >= 7);

    std::vector<nlohmann::ordered_json> documents;
    std::string error;
    for (const std::string& text : samples) {
        documents.push_back(nlohmann::ordered_json::parse(text));
        auto type = importOfl(documents.back(), {}, &error);
        REQUIRE_MESSAGE(type, error);
        exercise(*type, rng);
    }

    SlowestCall slowest;
    std::size_t accepted = 0;
    for (std::size_t i = 0; i < 1200; ++i) {
        const std::size_t which = i % samples.size();
        const nlohmann::json edited = nlohmann::json::parse(
            mutateJsonText(nlohmann::json::parse(samples[which]), samples[which], rng, i), nullptr, false);
        if (edited.is_discarded()) continue;  // damaged text: the JSON parser's job, not the importer's
        const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(edited.dump());
        slowest.run([&] {
            std::vector<std::string> warnings;
            static_cast<void>(isOflJson(doc));
            if (auto type = importOfl(doc, {}, &error, &warnings)) {
                ++accepted;
                exercise(*type, rng);
            }
        });
    }
    MESSAGE("OFL: " << accepted << " mutations were still accepted");
    CHECK(accepted > 0);
    CHECK(slowest.seconds() < 2.0);
}

TEST_CASE("mutation: GDTF archives and description.xml") {
    Rng rng(0x6D7F0003);
    const std::vector<std::string> xmls{test::movingHeadXml(), test::pixelBarXml()};
    std::vector<Bytes> archives;
    std::string error;
    for (const std::string& xml : xmls) {
        archives.push_back(test::makeGdtfArchive(xml));
        auto type = importGdtf(archives.back(), &error);
        REQUIRE_MESSAGE(type, error);
        exercise(*type, rng);
    }

    SlowestCall slowest;
    // 1) Damaged zip container.
    for (std::size_t i = 0; i < 700; ++i) {
        const Bytes mutated = mutateBytes(archives[i % archives.size()], rng, i);
        slowest.run([&] {
            if (auto type = importGdtf(mutated, &error)) exercise(*type, rng);
        });
    }
    // 2) Intact zip, damaged description.xml (attribute values, truncation, bit flips).
    std::size_t accepted = 0;
    for (std::size_t i = 0; i < 900; ++i) {
        const std::string xml = mutateXml(xmls[i % xmls.size()], rng, i);
        const Bytes archive = test::makeGdtfArchive(xml);
        slowest.run([&] {
            if (auto type = importGdtf(archive, &error)) {
                ++accepted;
                exercise(*type, rng);
            }
        });
    }
    MESSAGE("GDTF xml: " << accepted << " of 900 mutations were still accepted");
    CHECK(accepted > 0);
    CHECK(slowest.seconds() < 2.0);
}
