#include "app/ProjectIO.h"
#include "app/UserSettings.h"
#include "stage/ProjectFile.h"

#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace dmxviz;
using namespace dmxviz::app;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / "dmxviz_user_settings_test";
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

render::RenderSettings fastQuality() {
    render::RenderSettings q;
    q.volumetrics = false;
    q.maxMarchSteps = 33;
    q.hazePhaseG = 0.4f;
    q.bloom = false;
    q.tonemapper = render::Tonemapper::AcesPerChannel;
    return q;
}

// What an older version wrote into the project: the quality inside the environment block.
void writeOldProject(const fs::path& file, const render::RenderSettings& quality) {
    stage::Project project;
    project.environment["quality"] = qualityToJson(quality);
    std::string error;
    REQUIRE(stage::saveProject(project, file, &error));
}

struct ProjectState {
    stage::Scene scene;
    render::Environment environment;
    dmx::DmxManager dmx;
    fixtures::FixtureLibrary fixtures;
};

}  // namespace

TEST_CASE("UserSettings: quality survives a restart") {
    TempDir dir;
    const fs::path file = dir.path / "dmxviz_settings.json";
    std::string error;

    UserSettings first(file);
    render::RenderSettings quality = fastQuality();
    REQUIRE(first.saveIfChanged(quality, error));
    CHECK(fs::exists(file));

    UserSettings second(file);
    render::RenderSettings loaded;
    REQUIRE(second.load(loaded, error));
    CHECK(loaded.volumetrics == false);
    CHECK(loaded.maxMarchSteps == 33);
    CHECK(loaded.hazePhaseG == doctest::Approx(0.4f));
    CHECK(loaded.bloom == false);
    CHECK(loaded.tonemapper == render::Tonemapper::AcesPerChannel);
    CHECK(loaded.lensGlow == true);  // untouched fields keep their default
}

TEST_CASE("UserSettings: writes only when the quality changed") {
    TempDir dir;
    const fs::path file = dir.path / "dmxviz_settings.json";
    std::string error;
    UserSettings settings(file);
    render::RenderSettings quality;

    REQUIRE(settings.saveIfChanged(quality, error));
    CHECK_FALSE(fs::exists(file));  // defaults are not worth a file

    quality.bloom = false;
    REQUIRE(settings.saveIfChanged(quality, error));
    REQUIRE(fs::exists(file));
    fs::remove(file);
    REQUIRE(settings.saveIfChanged(quality, error));
    CHECK_FALSE(fs::exists(file));  // unchanged: nothing written

    quality.bloom = true;
    REQUIRE(settings.saveIfChanged(quality, error));
    CHECK(fs::exists(file));
}

TEST_CASE("UserSettings: missing file is fine, damaged file is reported and ignored") {
    TempDir dir;
    const fs::path file = dir.path / "dmxviz_settings.json";
    std::string error;
    render::RenderSettings quality = fastQuality();

    UserSettings settings(file);
    CHECK(settings.load(quality, error));
    CHECK(quality.maxMarchSteps == 33);  // kept

    std::ofstream(file) << "{ this is not json";
    CHECK_FALSE(settings.load(quality, error));
    CHECK_FALSE(error.empty());
    CHECK(quality.maxMarchSteps == 33);
}

TEST_CASE("UserSettings: an empty path keeps everything in memory") {
    UserSettings settings;
    std::string error;
    render::RenderSettings quality = fastQuality();
    CHECK(settings.load(quality, error));
    CHECK(settings.saveIfChanged(quality, error));
}

TEST_CASE("ProjectIO: render quality is no longer written into the project") {
    TempDir dir;
    ProjectState state;
    UserSettings settings(dir.path / "dmxviz_settings.json");
    render::RenderSettings quality = fastQuality();
    ProjectParts parts{state.scene, state.environment, state.dmx, state.fixtures, dir.path};
    parts.userSettings = &settings;
    parts.renderSettings = &quality;

    const fs::path file = dir.path / "show.dmxviz";
    std::string error;
    REQUIRE(saveProjectTo(parts, file, error));

    std::ifstream in(file);
    const nlohmann::json project = nlohmann::json::parse(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
    REQUIRE(project.contains("environment"));
    CHECK_FALSE(project["environment"].contains("quality"));
    CHECK(project["environment"].contains("hazeDensity"));  // the show's own look is still there
}

TEST_CASE("ProjectIO: environment.quality of an old project is adopted once, then the settings file wins") {
    TempDir dir;
    const fs::path settingsFile = dir.path / "dmxviz_settings.json";
    const fs::path oldProject = dir.path / "old.dmxviz";
    writeOldProject(oldProject, fastQuality());

    ProjectState state;
    UserSettings settings(settingsFile);
    render::RenderSettings quality;
    ProjectParts parts{state.scene, state.environment, state.dmx, state.fixtures, dir.path};
    parts.userSettings = &settings;
    parts.renderSettings = &quality;
    std::string error;

    // No settings file yet: the old project's quality becomes the user's, and is written to the file.
    REQUIRE(loadProjectFrom(parts, oldProject, error));
    CHECK(quality.maxMarchSteps == 33);
    CHECK(quality.bloom == false);
    REQUIRE(settings.saveIfChanged(quality, error));
    CHECK(fs::exists(settingsFile));

    // Another old project does not override what the user has now.
    render::RenderSettings other;
    other.maxMarchSteps = 50;
    other.bloom = true;
    const fs::path otherProject = dir.path / "other.dmxviz";
    writeOldProject(otherProject, other);
    REQUIRE(loadProjectFrom(parts, otherProject, error));
    CHECK(quality.maxMarchSteps == 33);
    CHECK(quality.bloom == false);

    // After a restart the file exists, so an old project is ignored from the start.
    UserSettings restarted(settingsFile);
    render::RenderSettings restartedQuality;
    REQUIRE(restarted.load(restartedQuality, error));
    CHECK(restartedQuality.maxMarchSteps == 33);
    parts.userSettings = &restarted;
    parts.renderSettings = &restartedQuality;
    REQUIRE(loadProjectFrom(parts, otherProject, error));
    CHECK(restartedQuality.maxMarchSteps == 33);
}
