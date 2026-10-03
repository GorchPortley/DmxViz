#pragma once
// Saving and loading a whole show (*.dmxviz) from the live application state.
//
// The stage module owns the file format (stage/ProjectFile.h); this file only
// gathers the pieces the app holds: the scene, the environment settings (opaque
// "environment" block) and the DMX configuration (opaque "dmx" block).

#include "app/UserSettings.h"
#include "dmx/DmxManager.h"
#include "fixtures/FixtureLibrary.h"
#include "render/RenderScene.h"
#include "render/RenderSettings.h"
#include "stage/Scene.h"

#include <filesystem>
#include <string>

namespace dmxviz::app {

// References to the application state that makes up a project.
struct ProjectParts {
    stage::Scene& scene;
    render::Environment& environment;
    dmx::DmxManager& dmx;
    fixtures::FixtureLibrary& fixtures;
    std::filesystem::path bundledFixtureDir;  // fixture types below this folder are not written into the project
    // Render quality is a per-user setting and is not saved in the project. Old projects stored it under
    // environment.quality: when both pointers are set, loading one adopts that block once (see UserSettings).
    UserSettings* userSettings = nullptr;
    render::RenderSettings* renderSettings = nullptr;
};

// Writes the project atomically. Returns false and sets `error` on failure.
bool saveProjectTo(const ProjectParts& parts, const std::filesystem::path& file, std::string& error);

// Replaces the scene, environment and (if the file has one) DMX configuration.
// On failure nothing is changed.
bool loadProjectFrom(const ProjectParts& parts, const std::filesystem::path& file, std::string& error);

}  // namespace dmxviz::app
