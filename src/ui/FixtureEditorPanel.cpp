#include "ui/FixtureEditorPanel.h"

#include "core/Log.h"
#include "fixtures/NativeFormat.h"
#include "ui/EditorContext.h"
#include "ui/FixtureEditRequests.h"
#include "ui/PanelTitles.h"
#include "ui/fixture_editor/EditorWidgets.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <format>

namespace dmxviz::ui {

using fixture_editor::FixtureTemplate;
using fixture_editor::Problem;
using fixture_editor::ProblemArea;

namespace {

constexpr float kSideBySideMinWidth = 820.0f;  // below this the preview becomes a tab

}  // namespace

FixtureEditorPanel::FixtureEditorPanel() {
    FixtureEditRequests::attachWindowFlag(&open);
}

FixtureEditorPanel::~FixtureEditorPanel() {
    FixtureEditRequests::detachWindowFlag(&open);
}

const char* FixtureEditorPanel::title() const {
    return kFixtureEditorTitle;
}

// ---------------------------------------------------------------------------
// Frame

void FixtureEditorPanel::draw(EditorContext& ctx) {
    pollSaveDialog();
    if (std::optional<std::string> id = FixtureEditRequests::take())
        requestOpen(ctx, {OpenRequest::Kind::Copy, std::move(*id)});

    drawToolbar(ctx);
    if (!doc_.isOpen()) {
        drawStartScreen(ctx);
    } else {
        revalidate(ctx);
        drawHeader(ctx);
        drawBody(ctx);
    }
    drawDiscardPopup(ctx);
}

fixture_editor::IdTakenFn FixtureEditorPanel::idTakenFn(EditorContext& ctx) const {
    const fixtures::FixtureLibrary* library = &ctx.fixtures;
    const std::string original = doc_.originalId();
    return [library, original](std::string_view id) { return id != original && library->find(id) != nullptr; };
}

void FixtureEditorPanel::touch(EditorContext& ctx) {
    doc_.touch(idTakenFn(ctx));
}

void FixtureEditorPanel::setStatus(std::string text, bool error) {
    status_ = std::move(text);
    statusIsError_ = error;
}

// ---------------------------------------------------------------------------
// Toolbar

void FixtureEditorPanel::drawToolbar(EditorContext& ctx) {
    if (ImGui::Button("New")) ImGui::OpenPopup("##newFixture");
    ImGui::SetItemTooltip("Start a new fixture from a template");
    drawNewMenu(ctx);
    ImGui::SameLine();
    if (ImGui::Button("Open")) ImGui::OpenPopup("##openFixture");
    ImGui::SetItemTooltip("Edit a copy of a fixture from the library");
    drawOpenMenu(ctx);

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    const bool isOpen = doc_.isOpen();
    ImGui::BeginDisabled(!isOpen || counts_.errors > 0);
    if (ImGui::Button("Apply")) applyToLibrary(ctx);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (isOpen && counts_.errors > 0)
            ImGui::SetTooltip("Fix the %d error(s) listed under Validation first.", counts_.errors);
        else if (isOpen)
            ImGui::SetTooltip("Add this fixture to the library, or replace the one with the same id.");
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!isOpen || dialogs_.busy() || !FileDialogs::available());
    if (ImGui::Button("Save as native file...")) requestSave();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(FileDialogs::available()
                              ? "Write the fixture to a .dmxviz-fixture.json file (self-contained)."
                              : "No file dialog available (on Linux install zenity or kdialog).");

    ImGui::SameLine();
    ImGui::BeginDisabled(!isOpen || !doc_.modified());
    if (ImGui::Button("Revert")) {
        doc_.revert();
        setStatus("Back to the last applied state.", false);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Throw away the edits since the fixture was opened or applied");

    ImGui::SameLine();
    ImGui::BeginDisabled(!isOpen);
    if (ImGui::Button("Close")) requestOpen(ctx, {OpenRequest::Kind::Close, {}});
    ImGui::EndDisabled();
}

void FixtureEditorPanel::drawNewMenu(EditorContext& ctx) {
    if (!ImGui::BeginPopup("##newFixture")) return;
    ImGui::TextDisabled("New fixture from");
    for (FixtureTemplate tmpl : fixture_editor::kAllTemplates) {
        if (ImGui::MenuItem(fixture_editor::templateName(tmpl))) requestOpen(ctx, {OpenRequest::Kind::New, {}, tmpl});
    }
    ImGui::EndPopup();
}

void FixtureEditorPanel::drawOpenMenu(EditorContext& ctx) {
    if (!ImGui::BeginPopup("##openFixture")) return;
    if (ImGui::IsWindowAppearing()) {
        openFilter_[0] = '\0';
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputTextWithHint("##openFilter", "Search fixtures", openFilter_, sizeof(openFilter_));
    ImGui::BeginChild("##openList", ImVec2(260.0f, 260.0f), ImGuiChildFlags_Borders);
    for (const fixtures::FixtureType* type : ctx.fixtures.search(openFilter_)) {
        ImGui::PushID(type->id.c_str());
        if (ImGui::Selectable(type->displayName().c_str())) {
            requestOpen(ctx, {OpenRequest::Kind::Copy, type->id});
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::EndPopup();
}

void FixtureEditorPanel::drawHeader(EditorContext& ctx) {
    (void)ctx;
    const fixtures::FixtureType& type = doc_.type();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", type.displayName().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("[%s]%s", type.id.c_str(), doc_.modified() ? "  modified" : "");
    ImGui::SameLine();
    if (counts_.errors > 0)
        ImGui::TextColored(fixture_editor::errorColor(), "  %d error%s", counts_.errors,
                           counts_.errors == 1 ? "" : "s");
    if (counts_.warnings > 0) {
        ImGui::SameLine();
        ImGui::TextColored(fixture_editor::warningColor(), "  %d warning%s", counts_.warnings,
                           counts_.warnings == 1 ? "" : "s");
    }
    if (counts_.errors == 0 && counts_.warnings == 0) {
        ImGui::SameLine();
        ImGui::TextColored(fixture_editor::okColor(), "  no problems");
    }
    if (!status_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, statusIsError_ ? fixture_editor::errorColor() : fixture_editor::okColor());
        ImGui::TextWrapped("%s", status_.c_str());
        ImGui::PopStyleColor();
    }
}

void FixtureEditorPanel::drawStartScreen(EditorContext& ctx) {
    ImGui::Spacing();
    ImGui::TextWrapped(
        "The fixture editor changes a copy of a fixture type. Press Edit in the Fixture Library, use Open above, "
        "or start a new fixture from a template:");
    ImGui::Spacing();
    for (FixtureTemplate tmpl : fixture_editor::kAllTemplates) {
        if (ImGui::Button(fixture_editor::templateName(tmpl), ImVec2(130.0f, 0.0f)))
            requestOpen(ctx, {OpenRequest::Kind::New, {}, tmpl});
        ImGui::SameLine();
    }
    ImGui::NewLine();
    if (!status_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, statusIsError_ ? fixture_editor::errorColor() : fixture_editor::okColor());
        ImGui::TextWrapped("%s", status_.c_str());
        ImGui::PopStyleColor();
    }
}

// ---------------------------------------------------------------------------
// Body

void FixtureEditorPanel::drawBody(EditorContext& ctx) {
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const bool sideBySide = availableWidth >= kSideBySideMinWidth;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float previewWidth = sideBySide ? std::clamp(availableWidth * 0.36f, 300.0f, 520.0f) : 0.0f;

    if (ImGui::BeginChild("##editorTabsArea",
                          ImVec2(sideBySide ? availableWidth - previewWidth - spacing : 0.0f, 0.0f)))
        drawTabs(ctx, !sideBySide);
    ImGui::EndChild();

    if (!sideBySide) return;
    ImGui::SameLine();
    if (ImGui::BeginChild("##previewArea", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        preview_.draw(ctx, doc_.type(), doc_.revision(), currentMode());
    ImGui::EndChild();
}

void FixtureEditorPanel::drawTabs(EditorContext& ctx, bool previewAsTab) {
    if (!ImGui::BeginTabBar("##fixtureEditorTabs")) return;

    // A tab that should come to the front (jump from the validation list) is selected for one frame.
    auto beginTab = [&](const char* label, Tab tab) {
        const ImGuiTabItemFlags flags = requestedTab_ == tab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        return ImGui::BeginTabItem(label, nullptr, flags);
    };

    bool changed = false;
    if (beginTab("General", Tab::General)) {
        if (ImGui::BeginChild("##generalScroll", ImVec2(0.0f, 0.0f))) changed |= general_.draw(doc_, idTakenFn(ctx));
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    if (beginTab("Geometry", Tab::Geometry)) {
        changed |= geometry_.draw(doc_, currentMode());
        ImGui::EndTabItem();
    }
    if (beginTab("Wheels", Tab::Wheels)) {
        changed |= wheels_.draw(doc_);
        ImGui::EndTabItem();
    }
    if (beginTab("Modes & Channels", Tab::Modes)) {
        changed |= modes_.draw(doc_);
        ImGui::EndTabItem();
    }

    char validationLabel[64];
    if (counts_.errors + counts_.warnings > 0)
        std::snprintf(validationLabel, sizeof(validationLabel), "Validation (%d)###validationTab",
                      counts_.errors + counts_.warnings);
    else
        std::snprintf(validationLabel, sizeof(validationLabel), "Validation###validationTab");
    if (beginTab(validationLabel, Tab::Validation)) {
        if (const std::optional<Problem> clicked = validation_.draw(problems_)) jumpTo(*clicked);
        ImGui::EndTabItem();
    }
    if (previewAsTab && beginTab("Preview", Tab::Preview)) {
        preview_.draw(ctx, doc_.type(), doc_.revision(), currentMode());
        ImGui::EndTabItem();
    }
    requestedTab_ = Tab::None;
    ImGui::EndTabBar();

    if (changed) touch(ctx);
}

void FixtureEditorPanel::jumpTo(const Problem& problem) {
    switch (problem.area) {
        case ProblemArea::General:
            requestedTab_ = Tab::General;
            break;
        case ProblemArea::Geometry:
            requestedTab_ = Tab::Geometry;
            if (!problem.geometry.empty()) geometry_.select(doc_.type(), problem.geometry);
            break;
        case ProblemArea::Wheels:
            requestedTab_ = Tab::Wheels;
            if (problem.wheel >= 0) wheels_.select(problem.wheel);
            break;
        case ProblemArea::Modes:
            requestedTab_ = Tab::Modes;
            modes_.select(problem.mode, problem.channel, problem.function);
            break;
    }
}

// ---------------------------------------------------------------------------
// Validation

void FixtureEditorPanel::revalidate(EditorContext& ctx) {
    if (doc_.revision() == validatedRevision_ && ctx.fixtures.size() == validatedLibrarySize_) return;
    fixture_editor::ValidationOptions options;
    options.idTaken = idTakenFn(ctx);
    problems_ = fixture_editor::validateFixture(doc_.type(), options);
    counts_ = fixture_editor::countProblems(problems_);
    validatedRevision_ = doc_.revision();
    validatedLibrarySize_ = ctx.fixtures.size();
}

// ---------------------------------------------------------------------------
// Opening, closing, applying

void FixtureEditorPanel::requestOpen(EditorContext& ctx, OpenRequest request) {
    if (!doc_.isOpen() || !doc_.modified()) {
        openNow(ctx, request);
        return;
    }
    pendingOpen_ = std::move(request);
    discardPopupRequested_ = true;
}

void FixtureEditorPanel::openNow(EditorContext& ctx, const OpenRequest& request) {
    switch (request.kind) {
        case OpenRequest::Kind::None:
            return;
        case OpenRequest::Kind::Close:
            doc_.close();
            setStatus({}, false);
            break;
        case OpenRequest::Kind::Copy: {
            const fixtures::FixtureType* type = ctx.fixtures.find(request.fixtureId);
            if (type == nullptr) {
                setStatus(std::format("The fixture \"{}\" is not in the library.", request.fixtureId), true);
                return;
            }
            doc_.openCopy(*type);
            setStatus(std::format("Editing a copy of {}. Apply writes it back to the library.", type->displayName()),
                      false);
            break;
        }
        case OpenRequest::Kind::New:
            doc_.openNew(request.tmpl, idTakenFn(ctx));
            setStatus(std::format("New fixture from the {} template.", fixture_editor::templateName(request.tmpl)),
                      false);
            break;
    }
    geometry_.reset();
    wheels_.reset();
    modes_.reset();
    preview_.reset();
    validatedRevision_ = ~0ull;
    requestedTab_ = Tab::General;
}

void FixtureEditorPanel::drawDiscardPopup(EditorContext& ctx) {
    if (discardPopupRequested_) {
        ImGui::OpenPopup("Discard changes?");
        discardPopupRequested_ = false;
    }
    if (!ImGui::BeginPopupModal("Discard changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("\"%s\" has changes that were not applied to the library.", doc_.type().displayName().c_str());
    ImGui::TextDisabled("Apply or save them first if you want to keep them.");
    ImGui::Spacing();
    if (ImGui::Button("Discard changes", ImVec2(140.0f, 0.0f))) {
        const OpenRequest request = pendingOpen_;
        pendingOpen_ = {};
        ImGui::CloseCurrentPopup();
        openNow(ctx, request);
    }
    ImGui::SameLine();
    if (ImGui::Button("Keep editing", ImVec2(140.0f, 0.0f))) {
        pendingOpen_ = {};
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void FixtureEditorPanel::applyToLibrary(EditorContext& ctx) {
    const bool replacing = ctx.fixtures.find(doc_.type().id) != nullptr;
    std::string error;
    if (!ctx.fixtures.addOrReplace(doc_.type(), &error)) {
        setStatus("Cannot apply: " + error, true);
        return;
    }
    doc_.markApplied();
    FixtureEditRequests::notifyApplied();
    log::info("ui", "fixture editor: {} \"{}\"", replacing ? "replaced" : "added", doc_.type().id);
    setStatus(std::format("{} {} in the library.", replacing ? "Replaced" : "Added", doc_.type().displayName()), false);
}

// ---------------------------------------------------------------------------
// Save as native file

void FixtureEditorPanel::requestSave() {
    std::string stem = fixtures::slugify(doc_.type().name);
    if (stem.empty()) stem = "fixture";
    const std::filesystem::path start = lastSaveDir_ / (stem + std::string(fixtures::kNativeFixtureExtension));
    dialogs_.requestSave("Save fixture", start,
                         {"DmxViz fixture (*.dmxviz-fixture.json)", "*.dmxviz-fixture.json", "All files", "*"});
}

void FixtureEditorPanel::pollSaveDialog() {
    const std::optional<FileDialogs::Result> result = dialogs_.poll();
    if (!result || result->path.empty() || !doc_.isOpen()) return;

    std::filesystem::path path = result->path;
    lastSaveDir_ = path.parent_path();
    if (!fixtures::isNativeFixturePath(path)) {
        path.replace_extension();
        path += std::string(fixtures::kNativeFixtureExtension);
    }
    // The file holds what the editor shows, applied or not.
    std::string error;
    if (fixtures::FixtureSerializer::saveFile(doc_.type(), path, &error)) {
        setStatus(std::format("Saved {}{}", path.filename().string(),
                              counts_.errors > 0 ? " (the fixture still has errors)" : ""),
                  counts_.errors > 0);
    } else {
        setStatus("Cannot save: " + error, true);
    }
}

}  // namespace dmxviz::ui
