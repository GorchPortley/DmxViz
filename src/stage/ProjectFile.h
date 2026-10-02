#pragma once
// DmxViz project files (*.dmxviz): the whole show in one JSON document
// (FR-STG-8).
//
//   {
//     "formatVersion": 1,
//     "scene": { "nextNodeId": 42, "layers": [...], "nodes": [...] },   // see SceneJson.h
//     "fixtureTypes": [ { "id": "acme/spot", "path": "fixtures/spot.dmxviz-fixture.json" } ],
//     "environment": { ... },   // opaque, owned by the app (haze, exposure...)
//     "dmx": { ... }            // opaque, owned by the DMX module (interfaces, routing)
//   }
//
// Paths (model files, fixture files) are stored relative to the project file
// and absolute in memory. Unknown top-level blocks are kept and written back,
// so files from newer versions round-trip; a newer formatVersion loads with a
// warning.

#include "stage/Scene.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dmxviz::stage {

inline constexpr int kProjectFormatVersion = 1;
inline constexpr const char* kProjectExtension = ".dmxviz";

// A fixture type the project uses. `path` is empty for types that come from
// the fixture library; otherwise the fixture file (absolute in memory).
struct FixtureTypeRef {
    std::string id;
    std::string path;
};

struct Project {
    Scene scene;
    nlohmann::json environment = nlohmann::json::object();
    nlohmann::json dmx = nlohmann::json::object();
    std::vector<FixtureTypeRef> fixtureTypes;
    // Top-level fields this version does not know, written back unchanged.
    nlohmann::json extra = nlohmann::json::object();
};

// projectDir: directory paths are made relative to (empty = keep paths as they are).
nlohmann::json projectToJson(const Project& project, const std::filesystem::path& projectDir);
std::optional<Project> projectFromJson(const nlohmann::json& j, const std::filesystem::path& projectDir,
                                       std::string* error, std::vector<std::string>* warnings = nullptr);

// Writes atomically (temporary file + rename). Returns false with a message on failure.
bool saveProject(const Project& project, const std::filesystem::path& file, std::string* error);
std::optional<Project> loadProject(const std::filesystem::path& file, std::string* error,
                                   std::vector<std::string>* warnings = nullptr);

}  // namespace dmxviz::stage
