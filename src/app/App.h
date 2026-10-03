#pragma once
// Application shell: owns the subsystems and drives the per-frame loop.
//
// Frame order (see docs/ARCHITECTURE.md "Frame loop"):
//   1. DMX snapshot               (dmx)
//   2. Simulation::update         (fixtures)  -> RenderScene meshes + beams
//   3. stage meshes               (stage)     -> cached inside Simulation
//   4. UI: menu, dock space, panels (ui)      -> the viewport panel calls Renderer::render
//   5. swapchain pass: ImGui      (sokol_imgui)
//
// To add a panel, see ui/Panel.h and App::createPanels().

#include "assets/AssetLibrary.h"
#include "dmx/DmxManager.h"
#include "dmx/DmxSnapshot.h"
#include "fixtures/FixtureLibrary.h"
#include "render/RenderScene.h"
#include "render/Renderer.h"
#include "stage/CommandStack.h"
#include "stage/Scene.h"
#include "stage/Selection.h"
#include "ui/EditorContext.h"
#include "ui/FileDialogs.h"
#include "ui/Panel.h"
#include "ui/ViewportPanel.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct sapp_event;

namespace dmxviz::app {

class Simulation;

struct AppOptions {
    std::optional<std::filesystem::path> screenshotPath;  // save a PNG of the window and quit
    int exitAfterFrames = 0;                              // 0 = run until closed
    int width = 1600;
    int height = 900;
    std::optional<std::filesystem::path> openFile;        // project to load at startup (default: demo show)
    std::optional<std::filesystem::path> saveProjectPath;  // write the startup show to this file (demo shows, smoke tests)
    bool testPattern = false;                             // drive all patched fixtures on (see DemoShow.h)
    std::filesystem::path executablePath;                 // argv[0]
};

AppOptions parseCommandLine(int argc, char** argv);

class App {
public:
    explicit App(AppOptions options);
    ~App();

    void init();
    void frame();
    void event(const sapp_event* ev);
    void cleanup();

    const AppOptions& options() const { return options_; }

private:
    // ---- start-up -----------------------------------------------------------
    void loadFixtureLibrary();
    void addDefaultDmxInputs();
    void createPanels();
    void loadStartupShow();
    void showStartView();

    // ---- per frame ------------------------------------------------------------
    void handleShortcuts();
    void pollFileDialogs();
    void drawMainMenu();
    void drawUnsavedChangesDialog();
    void updateWindowTitle();
    void maybeTakeScreenshot();

    // ---- project commands -----------------------------------------------------------
    void newProject();
    bool openProject(const std::filesystem::path& file);
    void requestOpenDialog();
    void saveProject();       // Save As when the project has no file yet
    void requestSaveAsDialog();
    bool saveToFile(const std::filesystem::path& file);
    void undo();
    void redo();
    // Runs `action` now, or after the user decided what to do with unsaved changes.
    void whenSafeToDiscard(std::function<void()> action);
    // Saves, then runs `action` (asks for a file name first when needed).
    void saveThen(std::function<void()> action);
    // Resets everything that refers to the old scene after it was replaced.
    void sceneReplaced();

    AppOptions options_;
    std::filesystem::path dataDir_;  // bundled data (fixtures, gobos, demo shows)
    double timeSeconds_ = 0.0;
    std::uint64_t frameIndex_ = 0;
    bool showImGuiDemo_ = false;
    bool resetLayout_ = false;
    std::string windowTitle_;

    // Subsystems. Declaration order matters: later members use earlier ones while being destroyed.
    assets::AssetLibrary assets_;
    fixtures::FixtureLibrary fixtures_;
    stage::Scene scene_;
    stage::CommandStack commands_{scene_};
    stage::Selection selection_;
    dmx::DmxManager dmx_;
    dmx::DmxSnapshot snapshot_;
    render::Renderer renderer_;
    render::Environment environment_;
    render::RenderScene frame_;
    std::unique_ptr<Simulation> simulation_;
    std::unique_ptr<ui::EditorContext> context_;
    std::vector<std::unique_ptr<ui::Panel>> panels_;
    ui::ViewportPanel* viewport_ = nullptr;  // owned by panels_

    // Files and unsaved-changes handling.
    ui::FileDialogs dialogs_;
    std::function<void()> pendingAction_;  // waiting for the "unsaved changes" answer
    std::function<void()> afterSave_;      // runs once a pending Save As succeeded
    bool openUnsavedDialog_ = false;
};

}  // namespace dmxviz::app
