#include "stage/ProjectFile.h"

#include "core/Limits.h"
#include "core/Log.h"
#include "stage/PathUtil.h"
#include "stage/SceneJson.h"

#include <format>
#include <fstream>
#include <iterator>
#include <system_error>

namespace dmxviz::stage {
namespace {

using json = nlohmann::json;

const char* const kKnownKeys[] = {"formatVersion", "scene", "fixtureTypes", "environment", "dmx"};

std::filesystem::path absoluteDir(const std::filesystem::path& file) {
    std::error_code ec;
    std::filesystem::path abs = std::filesystem::absolute(file, ec);
    if (ec) abs = file;
    return abs.lexically_normal().parent_path();
}

}  // namespace

json projectToJson(const Project& project, const std::filesystem::path& projectDir) {
    json j = project.extra.is_object() ? project.extra : json::object();
    j["formatVersion"] = kProjectFormatVersion;
    j["scene"] = sceneToJson(project.scene, JsonPathContext{projectDir});
    json types = json::array();
    for (const FixtureTypeRef& t : project.fixtureTypes) {
        json entry = {{"id", t.id}};
        if (!t.path.empty()) entry["path"] = relativePathUtf8(t.path, projectDir);
        types.push_back(std::move(entry));
    }
    j["fixtureTypes"] = std::move(types);
    j["environment"] = project.environment.is_null() ? json::object() : project.environment;
    j["dmx"] = project.dmx.is_null() ? json::object() : project.dmx;
    return j;
}

std::optional<Project> projectFromJson(const json& j, const std::filesystem::path& projectDir, std::string* error,
                                       std::vector<std::string>* warnings) {
    auto fail = [&](std::string msg) -> std::optional<Project> {
        if (error) *error = std::move(msg);
        return std::nullopt;
    };
    if (!j.is_object()) return fail("not a DmxViz project (the file is not a JSON object)");
    if (!j.contains("formatVersion")) return fail("not a DmxViz project (formatVersion is missing)");
    const json& version = j["formatVersion"];
    if (!version.is_number_integer() || version.get<long long>() < 1)
        return fail("formatVersion must be a positive whole number");
    if (version.get<long long>() > kProjectFormatVersion && warnings) {
        warnings->push_back(std::format(
            "the project was written by a newer DmxViz (format {}, this version reads {}); unknown data is kept "
            "but not shown",
            version.get<long long>(), kProjectFormatVersion));
    }

    Project p;
    if (j.contains("scene") &&
        !sceneFromJson(j["scene"], p.scene, JsonPathContext{projectDir}, error, warnings))
        return std::nullopt;

    if (j.contains("fixtureTypes")) {
        const json& types = j["fixtureTypes"];
        if (!types.is_array()) return fail("fixtureTypes: expected an array");
        for (std::size_t i = 0; i < types.size(); ++i) {
            const json& t = types[i];
            if (!t.is_object() || !t.contains("id") || !t["id"].is_string())
                return fail(std::format("fixtureTypes[{}]: expected an object with a string \"id\"", i));
            FixtureTypeRef ref;
            ref.id = t["id"].get<std::string>();
            if (t.contains("path")) {
                if (!t["path"].is_string()) return fail(std::format("fixtureTypes[{}].path: expected a string", i));
                ref.path = absolutePathUtf8(t["path"].get<std::string>(), projectDir);
            }
            p.fixtureTypes.push_back(std::move(ref));
        }
    }
    if (j.contains("environment")) {
        if (!j["environment"].is_object()) return fail("environment: expected an object");
        p.environment = j["environment"];
    }
    if (j.contains("dmx")) {
        if (!j["dmx"].is_object()) return fail("dmx: expected an object");
        p.dmx = j["dmx"];
    }
    p.extra = j;
    for (const char* key : kKnownKeys) p.extra.erase(key);
    return p;
}

bool saveProject(const Project& project, const std::filesystem::path& file, std::string* error) {
    const std::string text = projectToJson(project, absoluteDir(file)).dump(2) + "\n";
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "cannot write " + pathToUtf8(tmp);
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            if (error) *error = "error while writing " + pathToUtf8(tmp);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        if (error) *error = "cannot replace " + pathToUtf8(file);
        return false;
    }
    log::info("stage", "saved project {}", pathToUtf8(file));
    return true;
}

std::optional<Project> loadProject(const std::filesystem::path& file, std::string* error,
                                   std::vector<std::string>* warnings) {
    if (limits::fileTooLarge(file)) {
        if (error) *error = pathToUtf8(file.filename()) + ": file is too large";
        return std::nullopt;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + pathToUtf8(file);
        return std::nullopt;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (limits::jsonNestedTooDeeply(text)) {
        if (error) *error = pathToUtf8(file.filename()) + ": JSON is nested too deeply";
        return std::nullopt;
    }
    json j;
    try {
        j = json::parse(text);
    } catch (const json::parse_error& e) {
        if (error) *error = std::format("{}: invalid JSON ({})", pathToUtf8(file.filename()), e.what());
        return std::nullopt;
    }
    std::string err;
    std::optional<Project> project = projectFromJson(j, absoluteDir(file), &err, warnings);
    if (!project) {
        if (error) *error = std::format("{}: {}", pathToUtf8(file.filename()), err);
        return std::nullopt;
    }
    if (warnings) {
        for (const std::string& w : *warnings) log::warn("stage", "{}: {}", pathToUtf8(file.filename()), w);
    }
    return project;
}

}  // namespace dmxviz::stage
