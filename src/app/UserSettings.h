#pragma once
// Per-user settings that belong to the machine, not to a show: today the render quality.
//
// They live in a small JSON file (dmxviz_settings.json) next to the ImGui layout ini, so a project
// opened on another computer does not drag along quality choices made for the first one. Old
// projects stored them under environment.quality; that block is still read once as a fallback.

#include "render/RenderSettings.h"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <string>

namespace dmxviz::app {

class UserSettings {
public:
    // An empty path keeps everything in memory (screenshots, tests): nothing is read or written.
    explicit UserSettings(std::filesystem::path file = {});

    // Reads the file into `quality`. A missing file is normal (first start) and keeps `quality` as it
    // is; a damaged one is reported through `error` and also keeps it.
    bool load(render::RenderSettings& quality, std::string& error);

    // Writes the file when `quality` differs from what was last loaded or written. Cheap enough to
    // call every frame. Returns false and sets `error` if the file cannot be written.
    bool saveIfChanged(const render::RenderSettings& quality, std::string& error);

    // Old projects carry the quality in their environment block ("quality"). Adopts it into `quality`
    // only while this user has no settings of their own, so it happens once; the next saveIfChanged
    // then writes it to the settings file. Returns true when something was adopted.
    bool adoptLegacyQuality(const nlohmann::json& environment, render::RenderSettings& quality);

    const std::filesystem::path& file() const { return file_; }

private:
    std::filesystem::path file_;
    bool hasSettings_ = false;  // a settings file was read or written, or the legacy block was adopted
    std::string lastText_;      // serialized quality as it is on disk, to detect changes
};

// JSON form of the render quality (shared by the settings file and the legacy project block).
nlohmann::json qualityToJson(const render::RenderSettings& quality);

// Applies a quality object on top of `quality`; missing fields keep their value. Returns false and
// leaves `quality` untouched if the object is damaged.
bool qualityFromJson(const nlohmann::json& json, render::RenderSettings& quality);

}  // namespace dmxviz::app
