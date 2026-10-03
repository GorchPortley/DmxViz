#include "app/UserSettings.h"

#include "core/Limits.h"
#include "core/Log.h"
#include "stage/PathUtil.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace dmxviz::app {

namespace {

using nlohmann::json;

constexpr int kFileVersion = 1;

std::string serialize(const render::RenderSettings& quality) {
    json root = json::object();
    root["version"] = kFileVersion;
    root["quality"] = qualityToJson(quality);
    return root.dump(2) + "\n";
}

}  // namespace

json qualityToJson(const render::RenderSettings& q) {
    json j = json::object();
    j["volumetrics"] = q.volumetrics;
    j["minMarchSteps"] = q.minMarchSteps;
    j["maxMarchSteps"] = q.maxMarchSteps;
    j["marchPixelsPerStep"] = q.marchPixelsPerStep;
    j["hazePhaseG"] = q.hazePhaseG;
    j["clipBeamsAtFloor"] = q.clipBeamsAtFloor;
    j["lensGlow"] = q.lensGlow;
    j["bloom"] = q.bloom;
    j["tonemapper"] = static_cast<int>(q.tonemapper);
    return j;
}

bool qualityFromJson(const json& j, render::RenderSettings& quality) {
    if (!j.is_object()) return false;
    render::RenderSettings q = quality;  // only replaces `quality` when every field was readable
    try {
        q.volumetrics = j.value("volumetrics", q.volumetrics);
        q.minMarchSteps = j.value("minMarchSteps", q.minMarchSteps);
        q.maxMarchSteps = j.value("maxMarchSteps", q.maxMarchSteps);
        q.marchPixelsPerStep = j.value("marchPixelsPerStep", q.marchPixelsPerStep);
        q.hazePhaseG = j.value("hazePhaseG", q.hazePhaseG);
        q.clipBeamsAtFloor = j.value("clipBeamsAtFloor", q.clipBeamsAtFloor);
        q.lensGlow = j.value("lensGlow", q.lensGlow);
        q.bloom = j.value("bloom", q.bloom);
        const int tonemapper = j.value("tonemapper", static_cast<int>(q.tonemapper));
        if (tonemapper >= 0 && tonemapper <= static_cast<int>(render::Tonemapper::AcesPerChannel))
            q.tonemapper = static_cast<render::Tonemapper>(tonemapper);
    } catch (const json::exception&) {
        return false;
    }
    quality = q;
    return true;
}

UserSettings::UserSettings(std::filesystem::path file)
    : file_(std::move(file)), lastText_(serialize(render::RenderSettings{})) {}

bool UserSettings::load(render::RenderSettings& quality, std::string& error) {
    if (file_.empty()) return true;
    std::error_code ec;
    if (!std::filesystem::exists(file_, ec)) return true;  // first start

    std::ifstream in(file_, std::ios::binary);
    if (!in) {
        error = "cannot read " + stage::pathToUtf8(file_);
        return false;
    }
    // A damaged settings file is reported, never fatal: oversized or deeply nested files count as damaged.
    const std::string text = limits::fileTooLarge(file_, 16u << 20)
                                 ? std::string()
                                 : std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const json root = limits::jsonNestedTooDeeply(text) ? json(json::value_t::discarded)
                                                        : json::parse(text, nullptr, false);
    if (!root.is_object() || !root.contains("quality") || !qualityFromJson(root["quality"], quality)) {
        error = stage::pathToUtf8(file_) + " is damaged, using the default settings";
        return false;
    }
    hasSettings_ = true;
    lastText_ = serialize(quality);
    log::info("app", "loaded user settings from {}", stage::pathToUtf8(file_));
    return true;
}

bool UserSettings::saveIfChanged(const render::RenderSettings& quality, std::string& error) {
    if (file_.empty()) return true;
    const std::string text = serialize(quality);
    if (text == lastText_) return true;

    // Write next to the target and rename, so a crash never leaves half a file behind.
    std::filesystem::path tmp = file_;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            error = "cannot write " + stage::pathToUtf8(tmp);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file_, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        error = "cannot replace " + stage::pathToUtf8(file_);
        return false;
    }
    hasSettings_ = true;
    lastText_ = text;
    return true;
}

bool UserSettings::adoptLegacyQuality(const json& environment, render::RenderSettings& quality) {
    if (hasSettings_ || !environment.is_object() || !environment.contains("quality")) return false;
    if (!qualityFromJson(environment["quality"], quality)) {
        log::warn("app", "quality settings in the project are damaged, keeping the current ones");
        return false;
    }
    hasSettings_ = true;
    lastText_.clear();  // forces the next saveIfChanged to write the adopted values
    log::info("app", "adopted the render quality of an older project as user setting");
    return true;
}

}  // namespace dmxviz::app
