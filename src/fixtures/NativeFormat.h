#pragma once
// DmxViz's own fixture file format: `*.dmxviz-fixture.json`.
// The schema is documented in docs/FIXTURE_FORMAT.md.
//
// FixtureSerializer converts between FixtureType and JSON. Files use degrees
// for angles and may reference resources (gobo images, models) either as
// embedded base64 or as files relative to the fixture file; after loading,
// a FixtureType always holds the resource bytes itself.
//
// Errors are reported with the JSON path of the offending field, e.g.
//   "modes[0].channels[2].functions[1].dmx: expected [from, to]".

#include "fixtures/FixtureType.h"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace dmxviz::fixtures {

constexpr int kNativeFormatVersion = 1;
constexpr std::string_view kNativeFixtureExtension = ".dmxviz-fixture.json";

bool isNativeFixturePath(const std::filesystem::path& path);

struct NativeSaveOptions {
    // true: resource bytes are embedded as base64 (single self-contained file).
    // false: saveFile() writes them into "<file stem>.resources/" next to the
    // fixture file and references them with "file".
    bool embedResources = true;
};

class FixtureSerializer {
public:
    // Key order follows the documented schema (ordered_json) so saved files read well.
    static nlohmann::ordered_json toJson(const FixtureType& type);
    // baseDir resolves "file" resource references.
    static std::optional<FixtureType> fromJson(const nlohmann::ordered_json& json, const std::filesystem::path& baseDir,
                                               std::string* error);
    static std::optional<FixtureType> fromJson(const nlohmann::json& json, const std::filesystem::path& baseDir,
                                               std::string* error);

    static std::string toString(const FixtureType& type);
    static std::optional<FixtureType> fromString(std::string_view text, const std::filesystem::path& baseDir,
                                                 std::string* error);

    static std::optional<FixtureType> loadFile(const std::filesystem::path& path, std::string* error);
    static bool saveFile(const FixtureType& type, const std::filesystem::path& path, std::string* error,
                         const NativeSaveOptions& options = {});
};

}  // namespace dmxviz::fixtures
