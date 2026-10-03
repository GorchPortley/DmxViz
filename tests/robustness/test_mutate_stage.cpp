// Mutation robustness and limits of the project/scene/settings readers: project files, clipboard
// text, the DMX configuration block and the user settings file.

#include "Mutate.h"

#include "app/UserSettings.h"
#include "core/Limits.h"
#include "dmx/DmxManager.h"
#include "dmx/interfaces/EnttecProInterface.h"
#include "dmx/interfaces/OpenDmxInterface.h"
#include "stage/ProjectFile.h"
#include "stage/SceneJson.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace dmxviz;
using namespace dmxviz::robust;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

struct TempDir {
    fs::path path = fs::temp_directory_path() / "dmxviz_robust_stage";
    TempDir() {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

// A project nested `depth` levels deep: group > group > ... (as JSON text, built without recursion).
std::string nestedProject(std::size_t depth) {
    std::string text = R"({"formatVersion":1,"scene":{"nodes":[)";
    for (std::size_t i = 0; i < depth; ++i) text += R"({"kind":"group","children":[)";
    for (std::size_t i = 0; i < depth; ++i) text += "]}";
    text += "]}}";
    return text;
}

// What the app does with an accepted project: write it back out.
void useProject(const stage::Project& project, const fs::path& dir) {
    const json out = stage::projectToJson(project, dir);
    CHECK(out.is_object());
    CHECK(project.scene.nextId() >= 1);
}

}  // namespace

TEST_CASE("mutation: project files") {
    Rng rng(0x9201EC01);
    const fs::path dataDir = fs::path(DMXVIZ_DATA_DIR) / "shows";
    const std::vector<std::string> samples{
        readText(dataDir / "demo.dmxviz"),
        readText(fs::path(DMXVIZ_TEST_DATA_DIR) / "stage" / "forward_compat.dmxviz")};
    std::vector<json> documents;
    std::string error;
    for (const std::string& text : samples) {
        documents.push_back(json::parse(text));
        auto project = stage::projectFromJson(documents.back(), dataDir, &error);
        REQUIRE_MESSAGE(project, error);
        useProject(*project, dataDir);
    }

    SlowestCall slowest;
    std::size_t accepted = 0;
    for (std::size_t i = 0; i < 500; ++i) {
        const json edited = mutateJson(documents[i % documents.size()], rng);
        slowest.run([&] {
            std::vector<std::string> warnings;
            if (auto project = stage::projectFromJson(edited, dataDir, &error, &warnings)) {
                ++accepted;
                useProject(*project, dataDir);
            }
        });
    }
    CHECK(accepted > 0);
    CHECK(slowest.seconds() < 1.0);

    // The same through the file API, with damaged text (truncation, bit flips).
    TempDir temp;
    const fs::path file = temp.path / "mutated.dmxviz";
    for (std::size_t i = 0; i < 60; ++i) {
        const Bytes mutated = mutateBytes(toBytes(samples[i % samples.size()]), rng, i);
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(mutated.data()), static_cast<std::streamsize>(mutated.size()));
        }
        if (auto project = stage::loadProject(file, &error)) useProject(*project, temp.path);
    }
}

TEST_CASE("mutation: clipboard text") {
    Rng rng(0xC11B0002);
    const json demo = json::parse(readText(fs::path(DMXVIZ_DATA_DIR) / "shows" / "demo.dmxviz"));
    std::string error;
    auto project = stage::projectFromJson(demo, fs::path(DMXVIZ_DATA_DIR) / "shows", &error);
    REQUIRE_MESSAGE(project, error);
    std::vector<stage::NodeSnapshot> nodes;
    for (NodeId id : project->scene.roots()) nodes.push_back(project->scene.snapshot(id));
    const std::string text = stage::snapshotsToClipboardText(nodes);
    REQUIRE(stage::snapshotsFromClipboardText(text, &error));
    const json document = json::parse(text);
    for (std::size_t i = 0; i < 300; ++i)
        static_cast<void>(stage::snapshotsFromClipboardText(mutateJsonText(document, text, rng, i), &error));
}

TEST_CASE("mutation: DMX configuration block") {
    Rng rng(0xD3C0F103);
    // A configuration with every interface type, built the way the UI does.
    dmx::DmxManager source(dmx::DmxManagerOptions{false});
    for (const char* type : {"Art-Net", "sACN", "Loopback", dmx::EnttecProInterface::kTypeName, dmx::OpenDmxInterface::kTypeName})
        source.addInterface(type);
    source.setRoutes({{1, 1}, {2, 2}, {3, 3}, {4, 4}});
    const json config = source.saveConfig();
    REQUIRE(config["interfaces"].size() == 5);
    std::string error;
    {
        dmx::DmxManager target(dmx::DmxManagerOptions{false});
        REQUIRE_MESSAGE(target.loadConfig(config, error), error);
    }

    dmx::DmxManager target(dmx::DmxManagerOptions{false});
    std::size_t accepted = 0;
    for (std::size_t i = 0; i < 1500; ++i) {
        const json edited = mutateJson(config, rng);
        if (target.loadConfig(edited, error)) {
            ++accepted;
            static_cast<void>(target.saveConfig().dump());
            for (dmx::DmxInterface* iface : target.interfaces()) static_cast<void>(iface->summary());
        }
    }
    CHECK(accepted > 0);
}

TEST_CASE("mutation: user settings file") {
    Rng rng(0x05E77104);
    TempDir temp;
    const fs::path file = temp.path / "dmxviz_settings.json";
    render::RenderSettings quality;
    quality.maxMarchSteps = 33;  // not the defaults, so that something is written
    quality.bloom = false;
    {
        app::UserSettings writer(file);
        std::string error;
        REQUIRE_MESSAGE(writer.saveIfChanged(quality, error), error);
    }
    const std::string text = readText(file);
    REQUIRE(!text.empty());
    const json document = json::parse(text);
    for (std::size_t i = 0; i < 200; ++i) {
        const std::string mutated = mutateJsonText(document, text, rng, i);
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out << mutated;
        }
        app::UserSettings reader(file);
        render::RenderSettings loaded;
        std::string error;
        reader.load(loaded, error);  // either loads or reports "damaged"
        reader.adoptLegacyQuality(mutateJson(document, rng), loaded);
    }
}

TEST_CASE("limits: deeply nested projects and fixtures are rejected, not recursed into") {
    TempDir temp;
    std::string error;

    // 100000 nested groups: well past the node depth limit (and the JSON depth limit).
    const fs::path file = temp.path / "deep.dmxviz";
    {
        std::ofstream out(file, std::ios::binary);
        out << nestedProject(100000);
    }
    CHECK_FALSE(stage::loadProject(file, &error));
    CHECK(error.find("nested") != std::string::npos);

    // Just within the JSON limit but beyond the node depth limit: the scene reader refuses it.
    const json deep = json::parse(nestedProject(static_cast<std::size_t>(limits::kMaxNodeDepth) + 5));
    CHECK_FALSE(stage::projectFromJson(deep, temp.path, &error));
    CHECK(error.find("nested too deeply") != std::string::npos);
    // A reasonable depth still loads.
    const json shallow = json::parse(nestedProject(20));
    CHECK(stage::projectFromJson(shallow, temp.path, &error));

    // Clipboard text and arbitrary JSON text over the depth limit.
    CHECK_FALSE(stage::snapshotsFromClipboardText(std::string(100000, '[') + std::string(100000, ']'), &error));
    CHECK(limits::jsonNestedTooDeeply(std::string(1000, '[')));
    CHECK_FALSE(limits::jsonNestedTooDeeply(R"({"a":["]]]]]","[[[[["],"b":{"c":[1,2]}})"));  // brackets in strings
}

// 1,000,001 nodes: slow (about a second), so only with DMXVIZ_SLOW_TESTS=1 (the sanitizer CI job).
TEST_CASE("limits: a project with more than a million nodes is rejected" *
          doctest::skip(!std::getenv("DMXVIZ_SLOW_TESTS"))) {
    std::string text = R"({"formatVersion":1,"scene":{"nodes":[)";
    for (std::size_t i = 0; i <= limits::kMaxSceneNodes; ++i) text += i ? ",{}" : "{}";
    text += "]}}";
    std::string error;
    const json project = json::parse(text);
    CHECK_FALSE(stage::projectFromJson(project, fs::temp_directory_path(), &error));
    CHECK(error.find("more than") != std::string::npos);
}
