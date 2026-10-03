#pragma once
// Import of Open Fixture Library fixture definitions (JSON, schema 12.x).
// https://github.com/OpenLightingProject/open-fixture-library
//
// What maps where:
//   availableChannels / templateChannels + modes   -> DmxMode channels (fine channels become
//        16/24-bit offsets, switching channels become functions with a mode master,
//        matrix inserts are expanded per pixel or pixel group)
//   capabilities                                    -> ChannelFunctions (see OflImporter.cpp table)
//   wheels                                          -> Wheels (hex colours -> linear RGB, gobo images
//        from the OFL resources folder when available, otherwise bundled stand-in gobos)
//   matrix (pixelKeys / pixelCount / pixelGroups)   -> one Beam geometry per pixel + geometry groups
//   physical                                        -> dimensions, lumens, lens angles
//   categories                                      -> a generated geometry (moving head, par, bar, blinder)
//
// OFL files do not name their manufacturer: it comes from the folder
// (fixtures/<manufacturer>/<fixture>.json) or from the export plugin's
// "manufacturerKey"/"fixtureKey" fields.

#include "fixtures/FixtureType.h"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dmxviz::fixtures {

struct OflImportOptions {
    std::string manufacturerKey;  // "clay-paky"; default: JSON field or parent folder name
    std::string fixtureKey;       // "sharpy";    default: JSON field or file name
    std::string manufacturerName; // "Clay Paky"; default: derived from the key
    // OFL "resources" folder holding gobos/<key>.svg|png. Default: searched in the
    // parent folders of the imported file (works inside an OFL checkout).
    std::filesystem::path resourceDir;
    // Folder of bundled gobo images used for gobos without an available image.
    std::filesystem::path standInGoboDir;
};

bool isOflJson(const nlohmann::ordered_json& json);

std::optional<FixtureType> importOfl(const nlohmann::ordered_json& json, const OflImportOptions& options,
                                     std::string* error, std::vector<std::string>* warnings = nullptr);
std::optional<FixtureType> importOflFile(const std::filesystem::path& path, OflImportOptions options,
                                         std::string* error, std::vector<std::string>* warnings = nullptr);

}  // namespace dmxviz::fixtures
