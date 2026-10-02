#pragma once
// Application shell: owns the subsystems and drives the per-frame loop.
//
// Frame order (see docs/ARCHITECTURE.md "Frame loop"):
//   1. poll DMX snapshot          (dmx)
//   2. update fixture simulation  (fixtures)  -> RenderScene beams + meshes
//   3. build UI                   (ui)        -> viewports call Renderer::render
//   4. swapchain pass: ImGui      (sokol_imgui)

#include "assets/AssetLibrary.h"
#include "render/Renderer.h"
#include "ui/ViewportCamera.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

struct sapp_event;

namespace dmxviz::app {

struct AppOptions {
    std::optional<std::filesystem::path> screenshotPath;  // save a PNG of the window and quit
    int exitAfterFrames = 0;                              // 0 = run until closed
    int width = 1600;
    int height = 900;
    std::optional<std::filesystem::path> openFile;        // project or show to load at startup
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
    void drawMainMenu();
    void drawViewport();
    void drawStats();
    void maybeTakeScreenshot();

    AppOptions options_;
    assets::AssetLibrary assets_;
    render::Renderer renderer_;
    std::unique_ptr<render::ViewportTarget> viewport_;
    render::RenderScene scene_;
    ui::ViewportCamera camera_;
    std::uint64_t frameIndex_ = 0;
    bool showImGuiDemo_ = false;
};

}  // namespace dmxviz::app
