#pragma once
// The render sandbox application: a full-window viewport showing the demo
// stage, an ImGui overlay with render statistics and a few live controls, and
// a headless screenshot mode for CI.
//
//   render_sandbox [--screenshot out.png] [--frames N] [--beams N] [--haze x]
//                  [--camera front|side|top|audience|close] [--rig show|single|stress]
//                  [--time seconds] [--no-ui]

#include "DemoStage.h"
#include "assets/AssetLibrary.h"
#include "render/Renderer.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

struct sapp_event;

namespace dmxviz::sandbox {

struct SandboxOptions {
    std::optional<std::string> screenshotPath;
    int frames = 0;           // exit after this many frames (0 = run until closed)
    std::string rig = "show"; // show | single | stress (--beams N implies stress)
    int stressBeams = 0;      // beams in the stress rig
    float haze = 0.3f;
    std::string camera = "front";
    double startTime = 2.0;   // animation time of the first frame (s)
    bool showUi = true;
    int width = 1600;
    int height = 900;
};

SandboxOptions parseSandboxOptions(int argc, char** argv);

class SandboxApp {
public:
    explicit SandboxApp(SandboxOptions options) : options_(std::move(options)) {}

    void init();
    void frame();
    void event(const sapp_event* ev);
    void cleanup();

    const SandboxOptions& options() const { return options_; }

private:
    render::Camera makeCamera(int width, int height) const;
    void drawOverlay(double sceneMs, double renderMs);

    SandboxOptions options_;
    assets::AssetLibrary assets_;
    render::Renderer renderer_;
    std::unique_ptr<render::ViewportTarget> viewport_;
    render::RenderScene scene_;
    DemoStage stage_;
    std::uint64_t frameIndex_ = 0;
    std::uint64_t lastFrameTicks_ = 0;
    double cpuFrameMs_ = 0.0;
    double time_ = 0.0;
    bool animate_ = true;
    float orbit_ = 0.0f;  // extra camera yaw from mouse drag (radians)
    bool dragging_ = false;
};

}  // namespace dmxviz::sandbox
