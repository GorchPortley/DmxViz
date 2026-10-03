#include "app/ProjectIO.h"

#include "app/UserSettings.h"
#include "core/Log.h"
#include "stage/ProjectFile.h"

#include <nlohmann/json.hpp>

#include <set>
#include <utility>
#include <vector>

namespace dmxviz::app {

namespace {

using nlohmann::json;

json vec3ToJson(const glm::vec3& v) {
    return json::array({v.x, v.y, v.z});
}

glm::vec3 vec3FromJson(const json& j, const glm::vec3& fallback) {
    if (!j.is_array() || j.size() != 3) return fallback;
    return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
}

json environmentToJson(const render::Environment& e) {
    json j = json::object();
    j["hazeDensity"] = e.hazeDensity;
    j["hazeVariation"] = e.hazeVariation;
    j["ambient"] = vec3ToJson(e.ambient);
    j["background"] = vec3ToJson(e.background);
    j["exposure"] = e.exposure;
    j["bloomStrength"] = e.bloomStrength;
    j["beamBrightness"] = e.beamBrightness;
    j["showGrid"] = e.showGrid;
    return j;
}

// Missing or malformed fields keep their defaults, so older and newer files still load.
render::Environment environmentFromJson(const json& j) {
    render::Environment e;
    if (!j.is_object()) return e;
    try {
        e.hazeDensity = j.value("hazeDensity", e.hazeDensity);
        e.hazeVariation = j.value("hazeVariation", e.hazeVariation);
        if (j.contains("ambient")) e.ambient = vec3FromJson(j["ambient"], e.ambient);
        if (j.contains("background")) e.background = vec3FromJson(j["background"], e.background);
        e.exposure = j.value("exposure", e.exposure);
        e.bloomStrength = j.value("bloomStrength", e.bloomStrength);
        e.beamBrightness = j.value("beamBrightness", e.beamBrightness);
        e.showGrid = j.value("showGrid", e.showGrid);
    } catch (const json::exception& ex) {
        log::warn("app", "environment settings in the project are damaged ({}), using defaults", ex.what());
        return render::Environment{};
    }
    return e;
}

bool isInside(const std::filesystem::path& file, const std::filesystem::path& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    const std::filesystem::path relative =
        std::filesystem::weakly_canonical(file, ec).lexically_relative(std::filesystem::weakly_canonical(dir, ec));
    return !ec && !relative.empty() && *relative.begin() != "..";
}

// The fixture types the scene uses. Types imported from files outside the bundled
// library keep their file path so they can be re-imported when the project is opened.
std::vector<stage::FixtureTypeRef> usedFixtureTypes(const stage::Scene& scene, const fixtures::FixtureLibrary& library,
                                                    const std::filesystem::path& bundledDir) {
    std::set<std::string> ids;
    for (NodeId id : scene.nodesOfKind(stage::NodeKind::Fixture)) {
        const stage::Node* node = scene.find(id);
        const stage::FixtureContent* fixture = node ? node->as<stage::FixtureContent>() : nullptr;
        if (fixture != nullptr && !fixture->fixtureTypeId.empty()) ids.insert(fixture->fixtureTypeId);
    }
    std::vector<stage::FixtureTypeRef> refs;
    for (const std::string& id : ids) {
        stage::FixtureTypeRef ref;
        ref.id = id;
        const std::filesystem::path source = library.sourcePath(id);
        if (!source.empty() && !isInside(source, bundledDir)) ref.path = source.string();
        refs.push_back(std::move(ref));
    }
    return refs;
}

}  // namespace

bool saveProjectTo(const ProjectParts& parts, const std::filesystem::path& file, std::string& error) {
    // Project holds its scene by value: lend it ours for the duration of the call.
    stage::Project project;
    project.scene = std::move(parts.scene);
    project.environment = environmentToJson(parts.environment);
    project.dmx = parts.dmx.saveConfig();
    project.fixtureTypes = usedFixtureTypes(project.scene, parts.fixtures, parts.bundledFixtureDir);
    const bool ok = stage::saveProject(project, file, &error);
    parts.scene = std::move(project.scene);
    return ok;
}

bool loadProjectFrom(const ProjectParts& parts, const std::filesystem::path& file, std::string& error) {
    std::vector<std::string> warnings;
    std::optional<stage::Project> project = stage::loadProject(file, &error, &warnings);
    if (!project) return false;
    for (const std::string& w : warnings) log::warn("app", "{}: {}", file.filename().string(), w);

    // Fixture types that came from outside the bundled library.
    for (const stage::FixtureTypeRef& ref : project->fixtureTypes) {
        if (ref.path.empty() || parts.fixtures.find(ref.id) != nullptr) continue;
        std::string importError;
        if (!parts.fixtures.importFile(ref.path, &importError))
            log::warn("app", "could not load fixture type {} from {}: {}", ref.id, ref.path, importError);
    }

    parts.scene = std::move(project->scene);
    parts.environment = environmentFromJson(project->environment);
    // Projects from before the quality became a per-user setting still carry it: adopt it once, never write it.
    if (parts.userSettings != nullptr && parts.renderSettings != nullptr)
        parts.userSettings->adoptLegacyQuality(project->environment, *parts.renderSettings);
    if (!project->dmx.empty()) {
        std::string dmxError;
        if (parts.dmx.loadConfig(project->dmx, dmxError)) {
            const int failed = parts.dmx.startEnabledInterfaces();
            if (failed > 0) log::warn("dmx", "{} interface(s) of the project could not start", failed);
        } else {
            log::warn("dmx", "DMX configuration of the project was not applied: {}", dmxError);
        }
    }
    return true;
}

}  // namespace dmxviz::app
