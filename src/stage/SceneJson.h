#pragma once
// JSON form of scene nodes, shared by project files and the clipboard.
//
// A node is written as
//   { "id": 3, "kind": "truss", "name": "...", "transform": {...},
//     "visible": true, "locked": false, "layer": 0, "color": [r, g, b],
//     "materialOverride": {...},          // only when set
//     "truss": { ...kind specific... },   // key = kind name
//     "children": [ ... ] }
// Reading is tolerant: missing fields take their defaults, unknown fields are
// ignored, and nodes of unknown kinds are kept verbatim (UnknownContent) so a
// file from a newer DmxViz survives a load/save cycle.

#include "stage/Node.h"
#include "stage/Scene.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dmxviz::stage {

// How file paths inside node data (model files) are written and read.
struct JsonPathContext {
    // Directory paths are made relative to on save and resolved against on load.
    // Empty: paths are written and read unchanged.
    std::filesystem::path baseDir;
};

nlohmann::json nodeToJson(const NodeSnapshot& node, const JsonPathContext& paths = {});
// Throws nothing. `where` prefixes error messages (e.g. "scene.nodes[2]").
std::optional<NodeSnapshot> nodeFromJson(const nlohmann::json& j, const JsonPathContext& paths, std::string* error,
                                         std::vector<std::string>* warnings = nullptr,
                                         const std::string& where = "node");

nlohmann::json sceneToJson(const Scene& scene, const JsonPathContext& paths = {});
// Fills an empty scene. Returns false with an error message on invalid data.
bool sceneFromJson(const nlohmann::json& j, Scene& scene, const JsonPathContext& paths, std::string* error,
                   std::vector<std::string>* warnings = nullptr);

nlohmann::json materialToJson(const Material& m);

// Clipboard text for copy/paste between scenes or app instances.
std::string snapshotsToClipboardText(const std::vector<NodeSnapshot>& nodes);
std::optional<std::vector<NodeSnapshot>> snapshotsFromClipboardText(std::string_view text, std::string* error);

// The value nlohmann::json writes for a float: the shortest decimal that reads
// back as the same float (0.1f is written as 0.1, not 0.10000000149011612).
double jsonFloat(float f);

}  // namespace dmxviz::stage
