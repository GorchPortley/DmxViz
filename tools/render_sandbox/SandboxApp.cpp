#include "SandboxApp.h"

#include "Screenshot.h"
#include "core/Log.h"

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_time.h"
#include "imgui.h"
#include "sokol_imgui.h"

#include <cstdlib>
#include <string_view>

namespace dmxviz::sandbox {

SandboxOptions parseSandboxOptions(int argc, char** argv) {
    SandboxOptions o;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--screenshot") {
            o.screenshotPath = next();
            if (o.frames == 0) o.frames = 8;
        } else if (a == "--frames") {
            o.frames = std::atoi(next());
        } else if (a == "--beams") {
            o.stressBeams = std::atoi(next());
            o.rig = "stress";
        } else if (a == "--rig") {
            o.rig = next();
        } else if (a == "--haze") {
            o.haze = static_cast<float>(std::atof(next()));
        } else if (a == "--camera") {
            o.camera = next();
        } else if (a == "--time") {
            o.startTime = std::atof(next());
        } else if (a == "--no-ui") {
            o.showUi = false;
        } else if (a == "--width") {
            o.width = std::atoi(next());
        } else if (a == "--height") {
            o.height = std::atoi(next());
        } else {
            log::warn("sandbox", "unknown argument {}", a);
        }
    }
    return o;
}

void SandboxApp::init() {
    sg_desc gfx{};
    gfx.environment = sglue_environment();
    gfx.logger.func = slog_func;
    sg_setup(&gfx);
    stm_setup();

    simgui_desc_t gui{};
    gui.logger.func = slog_func;
    simgui_setup(&gui);
    ImGui::GetIO().IniFilename = nullptr;

    if (!renderer_.init()) log::error("sandbox", "renderer failed to initialise");
    viewport_ = std::make_unique<render::ViewportTarget>();

    DemoStage::Rig rig = DemoStage::Rig::Show;
    if (options_.rig == "single") rig = DemoStage::Rig::Single;
    if (options_.rig == "stress") rig = DemoStage::Rig::Stress;
    stage_.build(assets_, rig, options_.stressBeams > 0 ? options_.stressBeams : 1000);
    scene_.environment.hazeDensity = options_.haze;
    scene_.environment.hazeVariation = 0.35f;
    scene_.environment.ambient = glm::vec3(0.012f, 0.012f, 0.016f);
    scene_.environment.background = glm::vec3(0.0f);
    scene_.environment.bloomStrength = 0.06f;
    time_ = options_.startTime;
    log::info("sandbox", "demo stage: {} fixtures, camera '{}', haze {}", stage_.fixtureCount(), options_.camera,
              options_.haze);
}

render::Camera SandboxApp::makeCamera(int width, int height) const {
    glm::vec3 eye{0.0f, 1.7f, 17.0f}, target{0.0f, 4.2f, -3.0f};
    if (options_.camera == "audience") {
        eye = {0.0f, 5.0f, 26.0f};
        target = {0.0f, 4.0f, -4.0f};
    } else if (options_.camera == "side") {
        eye = {17.0f, 3.5f, 3.0f};
        target = {0.0f, 4.0f, -3.0f};
    } else if (options_.camera == "close") {
        eye = {6.5f, 2.2f, 4.5f};
        target = {0.0f, 2.2f, -1.0f};
    } else if (options_.camera == "top") {
        eye = {0.0f, 26.0f, 7.0f};
        target = {0.0f, 0.0f, -3.0f};
    }
    // Mouse orbit around the target's vertical axis.
    const glm::vec3 offset = eye - target;
    const float c = std::cos(orbit_), s = std::sin(orbit_);
    eye = target + glm::vec3(c * offset.x + s * offset.z, offset.y, -s * offset.x + c * offset.z);

    render::Camera cam;
    cam.position = eye;
    cam.fovY = degToRad(55.0f);
    cam.view = glm::lookAt(eye, target, glm::vec3(0, 1, 0));
    cam.projection = glm::perspective(cam.fovY, static_cast<float>(width) / static_cast<float>(height), cam.nearPlane,
                                      cam.farPlane);
    return cam;
}

void SandboxApp::frame() {
    const std::uint64_t frameStart = stm_now();
    const bool headless = options_.screenshotPath.has_value();
    // Screenshots use a fixed time step so the image is reproducible.
    const double dt = headless ? 1.0 / 60.0 : sapp_frame_duration();
    if (animate_ && frameIndex_ > 0) time_ += dt;

    const int width = sapp_width();
    const int height = sapp_height();

    // Scene update (what the fixture simulation does in the real app).
    const std::uint64_t sceneStart = stm_now();
    scene_.clear();
    scene_.environment.timeSeconds = time_;
    stage_.update(time_, scene_);
    const double sceneMs = stm_ms(stm_since(sceneStart));

    const std::uint64_t renderStart = stm_now();
    viewport_->resize(width, height);
    renderer_.render(*viewport_, makeCamera(width, height), scene_, assets_);
    const double renderMs = stm_ms(stm_since(renderStart));

    simgui_frame_desc_t fd{};
    fd.width = width;
    fd.height = height;
    fd.delta_time = sapp_frame_duration();
    fd.dpi_scale = sapp_dpi_scale();
    simgui_new_frame(&fd);
    ImGui::GetBackgroundDrawList()->AddImage(ImTextureRef(viewport_->imguiTexture()), ImVec2(0, 0),
                                             ImVec2(static_cast<float>(width), static_cast<float>(height)));
    if (options_.showUi) drawOverlay(sceneMs, renderMs);

    sg_pass pass{};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 1.0f};
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);
    simgui_render();
    sg_end_pass();

    if (headless && frameIndex_ + 1 == static_cast<std::uint64_t>(options_.frames)) {
        if (saveBackbufferPng(*options_.screenshotPath, width, height))
            log::info("sandbox", "saved screenshot {}", *options_.screenshotPath);
        else
            log::error("sandbox", "failed to save screenshot {}", *options_.screenshotPath);
    }
    sg_commit();

    cpuFrameMs_ = stm_ms(stm_since(frameStart));
    ++frameIndex_;
    if (options_.frames > 0 && frameIndex_ >= static_cast<std::uint64_t>(options_.frames)) sapp_request_quit();
}

void SandboxApp::drawOverlay(double sceneMs, double renderMs) {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.6f);
    if (ImGui::Begin("Render sandbox", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const render::RenderStats& s = renderer_.stats();
        ImGui::Text("CPU frame %.2f ms (scene %.2f, render record %.2f)", cpuFrameMs_, sceneMs, renderMs);
        ImGui::Text("%d x %d px", viewport_->width(), viewport_->height());
        ImGui::Text("draw calls %d", s.drawCalls);
        ImGui::Text("mesh instances %d", s.meshInstances);
        ImGui::Text("beams %d -> beam instances %d", s.beams, s.beamInstances);
        ImGui::Separator();
        render::Environment& env = scene_.environment;
        ImGui::SliderFloat("haze", &env.hazeDensity, 0.0f, 1.0f);
        ImGui::SliderFloat("haze variation", &env.hazeVariation, 0.0f, 1.0f);
        ImGui::SliderFloat("exposure", &env.exposure, 0.1f, 4.0f);
        ImGui::SliderFloat("beam brightness", &env.beamBrightness, 0.0f, 4.0f);
        ImGui::SliderFloat("bloom", &env.bloomStrength, 0.0f, 0.3f);
        ImGui::Checkbox("grid", &env.showGrid);
        ImGui::Checkbox("animate", &animate_);
        render::RenderSettings& rs = renderer_.settings();
        ImGui::Checkbox("volumetrics", &rs.volumetrics);
        ImGui::SameLine();
        ImGui::Checkbox("lens glow", &rs.lensGlow);
        ImGui::SameLine();
        ImGui::Checkbox("bloom##on", &rs.bloom);
        ImGui::SliderInt("max march steps", &rs.maxMarchSteps, rs.minMarchSteps, 48);
    }
    ImGui::End();
}

void SandboxApp::event(const sapp_event* ev) {
    if (simgui_handle_event(ev)) return;
    if (ev->type == SAPP_EVENTTYPE_MOUSE_DOWN && ev->mouse_button == SAPP_MOUSEBUTTON_LEFT) dragging_ = true;
    if (ev->type == SAPP_EVENTTYPE_MOUSE_UP && ev->mouse_button == SAPP_MOUSEBUTTON_LEFT) dragging_ = false;
    if (ev->type == SAPP_EVENTTYPE_MOUSE_MOVE && dragging_) orbit_ -= ev->mouse_dx * 0.005f;
}

void SandboxApp::cleanup() {
    viewport_.reset();
    renderer_.shutdown();
    simgui_shutdown();
    sg_shutdown();
}

}  // namespace dmxviz::sandbox
