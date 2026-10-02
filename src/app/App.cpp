#include "app/App.h"

#include "app/Screenshot.h"
#include "core/Log.h"
#include "ui/Theme.h"

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_time.h"
#include "imgui.h"
#include "sokol_imgui.h"

#include <cstdlib>
#include <string_view>

namespace dmxviz::app {

AppOptions parseCommandLine(int argc, char** argv) {
    AppOptions o;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--screenshot") {
            o.screenshotPath = next();
            if (o.exitAfterFrames == 0) o.exitAfterFrames = 10;
        } else if (a == "--frames") {
            o.exitAfterFrames = std::atoi(next());
        } else if (a == "--width") {
            o.width = std::atoi(next());
        } else if (a == "--height") {
            o.height = std::atoi(next());
        } else if (!a.empty() && a[0] != '-') {
            o.openFile = std::string(a);
        } else {
            log::warn("app", "unknown argument {}", a);
        }
    }
    return o;
}

App::App(AppOptions options) : options_(std::move(options)) {}
App::~App() = default;

void App::init() {
    sg_desc gfx{};
    gfx.environment = sglue_environment();
    gfx.logger.func = slog_func;
    sg_setup(&gfx);
    stm_setup();

    simgui_desc_t gui{};
    gui.logger.func = slog_func;
    gui.ini_filename = "dmxviz_layout.ini";
    simgui_setup(&gui);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    ui::applyTheme(sapp_dpi_scale());

    renderer_.init();
    viewport_ = std::make_unique<render::ViewportTarget>();

    camera_.position = {0.0f, 4.0f, 12.0f};
    scene_.environment.background = {0.01f, 0.01f, 0.015f};
    log::info("app", "DmxViz started ({}x{})", sapp_width(), sapp_height());
}

void App::frame() {
    simgui_frame_desc_t fd{};
    fd.width = sapp_width();
    fd.height = sapp_height();
    fd.delta_time = sapp_frame_duration();
    fd.dpi_scale = sapp_dpi_scale();
    simgui_new_frame(&fd);

    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    drawMainMenu();
    drawViewport();
    drawStats();
    if (showImGuiDemo_) ImGui::ShowDemoWindow(&showImGuiDemo_);

    sg_pass pass{};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {0.08f, 0.08f, 0.09f, 1.0f};
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);
    simgui_render();
    sg_end_pass();
    maybeTakeScreenshot();
    sg_commit();

    ++frameIndex_;
    if (options_.exitAfterFrames > 0 && frameIndex_ >= static_cast<std::uint64_t>(options_.exitAfterFrames))
        sapp_request_quit();
}

void App::event(const sapp_event* ev) { simgui_handle_event(ev); }

void App::cleanup() {
    viewport_.reset();
    renderer_.shutdown();
    simgui_shutdown();
    sg_shutdown();
}

void App::drawMainMenu() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) sapp_request_quit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("ImGui demo", nullptr, &showImGuiDemo_);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void App::drawViewport() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(1100, 700), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Viewport")) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float scale = sapp_dpi_scale();
        const int w = std::max(1, static_cast<int>(avail.x * scale));
        const int h = std::max(1, static_cast<int>(avail.y * scale));
        viewport_->resize(w, h);

        const float aspect = static_cast<float>(w) / static_cast<float>(h);
        camera_.view = glm::lookAt(camera_.position, glm::vec3(0.0f, 1.5f, 0.0f), glm::vec3(0, 1, 0));
        camera_.projection = glm::perspective(camera_.fovY, aspect, camera_.nearPlane, camera_.farPlane);
        renderer_.render(*viewport_, camera_, scene_, assets_);
        ImGui::Image(ImTextureRef(viewport_->imguiTexture()), avail);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void App::drawStats() {
    if (ImGui::Begin("Stats")) {
        const render::RenderStats& s = renderer_.stats();
        ImGui::Text("%.1f fps (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
        ImGui::Text("meshes %d  beams %d  draw calls %d", s.meshInstances, s.beams, s.drawCalls);
    }
    ImGui::End();
}

void App::maybeTakeScreenshot() {
    if (!options_.screenshotPath || options_.exitAfterFrames <= 0) return;
    if (frameIndex_ + 1 != static_cast<std::uint64_t>(options_.exitAfterFrames)) return;
    if (saveBackbufferPng(*options_.screenshotPath, sapp_width(), sapp_height()))
        log::info("app", "saved screenshot {}", options_.screenshotPath->string());
    else
        log::error("app", "failed to save screenshot {}", options_.screenshotPath->string());
}

}  // namespace dmxviz::app
