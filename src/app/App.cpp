#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#endif

#include "app/App.h"

#include "app/DemoShow.h"
#include "app/ProjectIO.h"
#include "app/Screenshot.h"
#include "app/Simulation.h"
#include "core/Log.h"
#include "dmx/interfaces/SacnInterface.h"
#include "stage/ProjectFile.h"
#include "ui/DockLayout.h"
#include "ui/DmxInterfacesPanel.h"
#include "ui/DmxMonitorPanel.h"
#include "ui/EnvironmentPanel.h"
#include "ui/FixtureEditorPanel.h"
#include "ui/FixtureLibraryPanel.h"
#include "ui/InspectorPanel.h"
#include "ui/PatchPanel.h"
#include "ui/TestConsolePanel.h"
#include "ui/LogPanel.h"
#include "ui/OutlinerPanel.h"
#include "ui/PanelTitles.h"
#include "ui/StageMenu.h"
#include "ui/Theme.h"

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_time.h"
#include "imgui.h"
#include "ImGuizmo.h"
#include "sokol_imgui.h"

#include <cstdlib>
#include <string_view>
#include <vector>

namespace dmxviz::app {

namespace {

// Folder of the running executable; bundled data lives next to it.
std::filesystem::path executableDirectory(const std::filesystem::path& argv0) {
#ifdef _WIN32
    wchar_t buffer[4096];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length > 0 && length < std::size(buffer)) return std::filesystem::path(buffer).parent_path();
#else
    std::error_code ec;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) return self.parent_path();
#endif
    if (!argv0.empty()) return std::filesystem::absolute(argv0).parent_path();
    return std::filesystem::current_path();
}

// The bundled data folder: next to the executable (CMake copies it there), else the source tree.
std::filesystem::path findDataDirectory(const std::filesystem::path& exeDir) {
    std::vector<std::filesystem::path> candidates = {exeDir / "data", std::filesystem::current_path() / "data"};
#ifdef DMXVIZ_SOURCE_DATA_DIR
    candidates.emplace_back(DMXVIZ_SOURCE_DATA_DIR);
#endif
    for (const std::filesystem::path& dir : candidates) {
        std::error_code ec;
        if (std::filesystem::is_directory(dir / "fixtures", ec)) return dir;
    }
    return {};
}

constexpr const char* kProjectFilterName = "DmxViz projects";
constexpr const char* kProjectFilterGlob = "*.dmxviz";

}  // namespace

AppOptions parseCommandLine(int argc, char** argv) {
    AppOptions o;
    if (argc > 0 && argv[0] != nullptr) o.executablePath = argv[0];
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
        } else if (a == "--save-project") {
            o.saveProjectPath = next();
        } else if (a == "--verbose") {
            log::setMinLevel(log::Level::Debug);
        } else if (a == "--test-pattern") {
            o.testPattern = true;
        } else if (!a.empty() && a[0] != '-') {
            o.openFile = std::string(a);
        } else {
            log::warn("app", "unknown argument {}", a);
        }
    }
    return o;
}

namespace {

// Both files sit in the working directory. Screenshots use neither: they start from the defaults.
constexpr const char* kLayoutIniFile = "dmxviz_layout.ini";
constexpr const char* kUserSettingsFile = "dmxviz_settings.json";

}  // namespace

App::App(AppOptions options)
    : options_(std::move(options)),
      userSettings_(options_.screenshotPath ? std::filesystem::path{} : std::filesystem::path{kUserSettingsFile}) {}
App::~App() = default;

// ---------------------------------------------------------------------------
// Start-up and shut-down

void App::init() {
    sg_desc gfx{};
    gfx.environment = sglue_environment();
    gfx.logger.func = slog_func;
    sg_setup(&gfx);
    stm_setup();

    simgui_desc_t gui{};
    gui.logger.func = slog_func;
    // Screenshots always start from the default layout.
    gui.ini_filename = options_.screenshotPath ? nullptr : kLayoutIniFile;
    // Headless screenshot runs have nobody to answer a native file dialog.
    if (options_.screenshotPath) ui::FileDialogs::setEnabled(false);
    simgui_setup(&gui);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    ui::applyTheme(sapp_dpi_scale());

    renderer_.init();
    std::string settingsError;
    if (!userSettings_.load(renderer_.settings(), settingsError)) log::warn("app", "{}", settingsError);
    log::info("app", "DmxViz started ({}x{})", sapp_width(), sapp_height());

    loadFixtureLibrary();
    addDefaultDmxInputs();

    simulation_ = std::make_unique<Simulation>(assets_, fixtures_);
    simulation_->setSelection(&selection_);

    context_ = std::make_unique<ui::EditorContext>(ui::EditorContext{.scene = scene_,
                                                                     .commands = commands_,
                                                                     .selection = selection_,
                                                                     .fixtures = fixtures_,
                                                                     .dmx = dmx_,
                                                                     .dmxSnapshot = snapshot_,
                                                                     .assets = assets_,
                                                                     .renderer = renderer_,
                                                                     .environment = environment_,
                                                                     .frame = frame_});
    createPanels();
    loadStartupShow();
}

void App::cleanup() {
    // GPU objects (viewport targets) must go before sokol shuts down.
    panels_.clear();
    viewport_ = nullptr;
    context_.reset();
    simulation_.reset();
    saveUserSettings(false);
    renderer_.shutdown();
    simgui_shutdown();
    sg_shutdown();
}

void App::loadFixtureLibrary() {
    dataDir_ = findDataDirectory(executableDirectory(options_.executablePath));
    if (dataDir_.empty()) {
        log::warn("app", "bundled data folder not found: no fixture library, no gobos");
        return;
    }
    fixtures_.setStandInGoboDir(dataDir_ / "gobos");
    const int loaded = fixtures_.loadDirectory(dataDir_ / "fixtures");
    log::info("app", "fixture library: {} types from {}", loaded, (dataDir_ / "fixtures").string());
    for (const fixtures::FixtureLibrary::LoadError& e : fixtures_.errors())
        log::warn("app", "fixture file {}: {}", e.path.string(), e.message);
    fixtures_.clearErrors();
}

void App::addDefaultDmxInputs() {
    // Art-Net and sACN listen on all network interfaces. A port that cannot be bound is only logged.
    const dmx::InterfaceId artnet = dmx_.addInterface("Art-Net");
    const dmx::InterfaceId sacn = dmx_.addInterface("sACN");
    if (auto* iface = dmx_.findInterfaceAs<dmx::SacnInterface>(sacn)) {
        dmx::SacnConfig config = iface->config();
        config.universes = {1, 2, 3, 4};
        iface->setConfig(config);
    }
    for (const dmx::InterfaceId id : {artnet, sacn}) {
        if (id == 0) continue;
        std::string error;
        if (!dmx_.startInterface(id, error)) log::warn("dmx", "interface {} not started: {}", id, error);
    }
}

void App::createPanels() {
    auto viewport = std::make_unique<ui::ViewportPanel>();
    viewport_ = viewport.get();
    panels_.push_back(std::move(viewport));

    context_->viewportCamera = &viewport_->camera();

    panels_.push_back(std::make_unique<ui::OutlinerPanel>());
    panels_.push_back(std::make_unique<ui::InspectorPanel>());
    panels_.push_back(std::make_unique<ui::EnvironmentPanel>());
    panels_.push_back(std::make_unique<ui::FixtureLibraryPanel>());
    panels_.push_back(std::make_unique<ui::FixtureEditorPanel>());
    panels_.push_back(std::make_unique<ui::PatchPanel>());
    panels_.push_back(std::make_unique<ui::TestConsolePanel>());
    panels_.push_back(std::make_unique<ui::DmxMonitorPanel>());
    panels_.push_back(std::make_unique<ui::DmxInterfacesPanel>());
    panels_.push_back(std::make_unique<ui::LogPanel>());
}

void App::loadStartupShow() {
    bool loaded = false;
    if (options_.openFile) loaded = openProject(*options_.openFile);
    if (!loaded) {
        buildDemoShow(scene_, fixtures_, environment_);
        sceneReplaced();
    }
    if (options_.testPattern) applyTestPattern(scene_, fixtures_, dmx_);
    if (options_.saveProjectPath) saveToFile(*options_.saveProjectPath);
    showStartView();
}

void App::showStartView() {
    if (viewport_ == nullptr) return;
    viewport_->showPreset(*context_, ui::ViewportCamera::Preset::Perspective);
}

// ---------------------------------------------------------------------------
// Frame

void App::frame() {
    const float dt = static_cast<float>(sapp_frame_duration());
    timeSeconds_ += dt;

    simgui_frame_desc_t fd{};
    fd.width = sapp_width();
    fd.height = sapp_height();
    fd.delta_time = sapp_frame_duration();
    fd.dpi_scale = sapp_dpi_scale();
    simgui_new_frame(&fd);
    ImGuizmo::BeginFrame();
    context_->dpiScale = sapp_dpi_scale();

    handleShortcuts();
    pollFileDialogs();

    // 1-3: DMX -> fixture simulation + stage meshes -> this frame's RenderScene.
    dmx_.snapshot(snapshot_);
    simulation_->update(scene_, snapshot_, dt, timeSeconds_, frame_);
    frame_.lines.clear();
    frame_.environment = environment_;
    frame_.environment.timeSeconds = timeSeconds_;

    // 4: UI. The menu comes first so the dock space starts below it.
    drawMainMenu();
    const ImGuiID dockspace = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    ui::ensureDefaultDockLayout(dockspace, resetLayout_);
    resetLayout_ = false;
    for (const std::unique_ptr<ui::Panel>& panel : panels_) panel->show(*context_);
    drawUnsavedChangesDialog();
    if (showImGuiDemo_) ImGui::ShowDemoWindow(&showImGuiDemo_);
    updateWindowTitle();
    saveUserSettings(true);

    // 5: swapchain pass with ImGui on top.
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
    // Unattended runs (--frames / --screenshot) end without asking about unsaved changes.
    if (options_.exitAfterFrames > 0 && frameIndex_ >= static_cast<std::uint64_t>(options_.exitAfterFrames))
        sapp_quit();
}

void App::event(const sapp_event* ev) {
    if (ev->type == SAPP_EVENTTYPE_QUIT_REQUESTED && context_ && context_->dirty() && !options_.screenshotPath) {
        sapp_cancel_quit();
        whenSafeToDiscard([] { sapp_quit(); });
        return;
    }
    if (ev->type == SAPP_EVENTTYPE_FILES_DROPPED && context_) {
        for (int i = 0; i < sapp_get_num_dropped_files(); ++i) {
            // sokol hands out UTF-8 paths on every platform.
            const std::string utf8 = sapp_get_dropped_file_path(i);
            const std::filesystem::path file(std::u8string(utf8.begin(), utf8.end()));
            if (file.extension() == stage::kProjectExtension) {
                whenSafeToDiscard([this, file] { openProject(file); });
            } else {
                std::string error;
                if (const auto id = fixtures_.importFile(file, &error))
                    log::info("app", "imported fixture type {}", *id);
                else
                    log::warn("app", "cannot use dropped file {}: {}", file.string(), error);
            }
            break;  // one file per drop is enough
        }
        return;
    }
    simgui_handle_event(ev);
}

void App::handleShortcuts() {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || !io.KeyCtrl || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (io.KeyShift)
            redo();
        else
            undo();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) redo();
    if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (io.KeyShift)
            requestSaveAsDialog();
        else
            saveProject();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_O, false)) whenSafeToDiscard([this] { requestOpenDialog(); });
    if (ImGui::IsKeyPressed(ImGuiKey_N, false)) newProject();
    if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) whenSafeToDiscard([] { sapp_quit(); });
}

void App::drawMainMenu() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New", "Ctrl+N")) newProject();
        if (ImGui::MenuItem("Open...", "Ctrl+O")) whenSafeToDiscard([this] { requestOpenDialog(); });
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S")) saveProject();
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) requestSaveAsDialog();
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) whenSafeToDiscard([] { sapp_quit(); });
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        const std::string undoLabel = commands_.canUndo() ? "Undo " + commands_.undoName() : "Undo";
        const std::string redoLabel = commands_.canRedo() ? "Redo " + commands_.redoName() : "Redo";
        if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, commands_.canUndo())) undo();
        if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, commands_.canRedo())) redo();
        ImGui::EndMenu();
    }
    ui::drawStageMenu(*context_);  // "Add" and "Tools"
    if (ImGui::BeginMenu("View")) {
        for (const std::unique_ptr<ui::Panel>& panel : panels_) ImGui::MenuItem(panel->title(), nullptr, &panel->open);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset layout")) {
            resetLayout_ = true;
            for (const std::unique_ptr<ui::Panel>& panel : panels_) panel->open = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("ImGui demo", nullptr, &showImGuiDemo_);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void App::updateWindowTitle() {
    const std::string name = context_->projectPath.empty() ? "Untitled" : context_->projectPath.filename().string();
    const std::string title = "DmxViz - " + name + (context_->dirty() ? " *" : "");
    if (title == windowTitle_) return;
    windowTitle_ = title;
    sapp_set_window_title(title.c_str());
}

void App::saveUserSettings(bool idleOnly) {
    if (idleOnly && ImGui::IsAnyItemActive()) return;
    std::string error;
    if (userSettings_.saveIfChanged(renderer_.settings(), error)) {
        settingsError_.clear();
    } else if (error != settingsError_) {  // it is retried every frame, but reported once
        settingsError_ = error;
        log::error("app", "cannot save user settings: {}", error);
    }
}

void App::maybeTakeScreenshot() {
    if (!options_.screenshotPath || options_.exitAfterFrames <= 0) return;
    if (frameIndex_ + 1 != static_cast<std::uint64_t>(options_.exitAfterFrames)) return;
    if (saveBackbufferPng(*options_.screenshotPath, sapp_width(), sapp_height()))
        log::info("app", "saved screenshot {}", options_.screenshotPath->string());
    else
        log::error("app", "failed to save screenshot {}", options_.screenshotPath->string());
}

// ---------------------------------------------------------------------------
// Project commands

void App::sceneReplaced() {
    simulation_->invalidate();
    // Node ids restart in a new scene, so programmer values from the old one would hit other fixtures.
    dmx_.clearProgrammer();
    selection_.clear();
    commands_.clear();
    commands_.markSaved();
}

void App::newProject() {
    whenSafeToDiscard([this] {
        scene_.clear();
        environment_ = render::Environment{};
        context_->projectPath.clear();
        context_->settingsDirty = false;
        sceneReplaced();
        log::info("app", "new project");
    });
}

bool App::openProject(const std::filesystem::path& file) {
    ProjectParts parts{scene_, environment_, dmx_, fixtures_, dataDir_ / "fixtures"};
    parts.userSettings = &userSettings_;  // for the quality block of old projects
    parts.renderSettings = &renderer_.settings();
    std::string error;
    if (!loadProjectFrom(parts, file, error)) {
        log::error("app", "cannot open {}: {}", file.string(), error);
        return false;
    }
    context_->projectPath = file;
    context_->settingsDirty = false;
    sceneReplaced();
    showStartView();
    log::info("app", "opened {} ({} nodes)", file.string(), scene_.nodeCount());
    return true;
}

bool App::saveToFile(const std::filesystem::path& file) {
    const ProjectParts parts{scene_, environment_, dmx_, fixtures_, dataDir_ / "fixtures"};
    std::string error;
    if (!app::saveProjectTo(parts, file, error)) {
        log::error("app", "cannot save {}: {}", file.string(), error);
        return false;
    }
    commands_.markSaved();
    context_->settingsDirty = false;
    context_->projectPath = file;
    log::info("app", "saved {}", file.string());
    return true;
}

void App::saveProject() {
    if (context_->projectPath.empty())
        requestSaveAsDialog();
    else
        saveToFile(context_->projectPath);
}

void App::requestOpenDialog() {
    if (options_.screenshotPath) return;  // no dialogs in unattended runs
    const std::filesystem::path start =
        context_->projectPath.empty() ? std::filesystem::current_path() : context_->projectPath.parent_path();
    dialogs_.requestOpen("Open project", start, {kProjectFilterName, kProjectFilterGlob, "All files", "*"});
}

void App::requestSaveAsDialog() {
    if (options_.screenshotPath) return;
    const std::filesystem::path start =
        context_->projectPath.empty() ? std::filesystem::current_path() / "show.dmxviz" : context_->projectPath;
    dialogs_.requestSave("Save project as", start, {kProjectFilterName, kProjectFilterGlob, "All files", "*"});
}

void App::pollFileDialogs() {
    const std::optional<ui::FileDialogs::Result> result = dialogs_.poll();
    if (!result) return;
    if (result->path.empty()) {  // cancelled
        afterSave_ = nullptr;
        return;
    }
    if (result->kind == ui::FileDialogs::Kind::Open) {
        openProject(result->path);
        return;
    }
    std::filesystem::path file = result->path;
    if (file.extension().empty()) file += stage::kProjectExtension;
    const bool saved = saveToFile(file);
    std::function<void()> after = std::move(afterSave_);
    afterSave_ = nullptr;
    if (saved && after) after();
}

void App::undo() {
    log::debug("app", "undo '{}'", commands_.undoName());
    if (!commands_.undo()) return;
    selection_.prune(scene_);
}

void App::redo() {
    log::debug("app", "redo '{}'", commands_.redoName());
    if (!commands_.redo()) return;
    selection_.prune(scene_);
}

// ---------------------------------------------------------------------------
// Unsaved changes

void App::whenSafeToDiscard(std::function<void()> action) {
    if (!context_->dirty() || options_.screenshotPath) {
        action();
        return;
    }
    pendingAction_ = std::move(action);
    openUnsavedDialog_ = true;
}

void App::saveThen(std::function<void()> action) {
    if (context_->projectPath.empty()) {
        afterSave_ = std::move(action);
        requestSaveAsDialog();
    } else if (saveToFile(context_->projectPath)) {
        action();
    }
}

void App::drawUnsavedChangesDialog() {
    static constexpr const char* kTitle = "Unsaved changes";
    if (openUnsavedDialog_) {
        ImGui::OpenPopup(kTitle);
        openUnsavedDialog_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted("The project has changes that are not saved.");
    ImGui::Spacing();
    if (ImGui::Button("Save")) {
        ImGui::CloseCurrentPopup();
        saveThen(std::move(pendingAction_));
        pendingAction_ = nullptr;
    }
    ImGui::SameLine();
    if (ImGui::Button("Don't save")) {
        ImGui::CloseCurrentPopup();
        std::function<void()> action = std::move(pendingAction_);
        pendingAction_ = nullptr;
        if (action) action();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
        pendingAction_ = nullptr;
    }
    ImGui::EndPopup();
}

}  // namespace dmxviz::app
