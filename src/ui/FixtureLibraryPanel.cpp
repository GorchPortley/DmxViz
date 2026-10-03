#include "ui/FixtureLibraryPanel.h"

#include "core/Log.h"
#include "ui/EditorContext.h"
#include "ui/FixtureDragDrop.h"
#include "ui/FixtureEditRequests.h"
#include "ui/FixtureSpawner.h"
#include "ui/PanelTitles.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>

namespace dmxviz::ui {

namespace {

constexpr float kDetailLines = 5.5f;

// "3 modes, 16-24 ch" into a caller-owned buffer (no allocation while drawing).
void formatModeSummary(const fixtures::FixtureType& type, char* out, std::size_t size) {
    if (type.modes.empty()) {
        std::snprintf(out, size, "no DMX modes");
        return;
    }
    int lowest = type.modes.front().footprint;
    int highest = lowest;
    for (const fixtures::DmxMode& mode : type.modes) {
        lowest = std::min(lowest, mode.footprint);
        highest = std::max(highest, mode.footprint);
    }
    const int count = static_cast<int>(type.modes.size());
    if (lowest == highest)
        std::snprintf(out, size, "%d mode%s, %d ch", count, count == 1 ? "" : "s", lowest);
    else
        std::snprintf(out, size, "%d modes, %d-%d ch", count, lowest, highest);
}

ImVec4 errorColor() {
    return ImVec4(1.0f, 0.45f, 0.40f, 1.0f);
}

}  // namespace

const char* FixtureLibraryPanel::title() const {
    return kFixtureLibraryTitle;
}

void FixtureLibraryPanel::draw(EditorContext& ctx) {
    pollImportDialog(ctx);
    drawToolbar(ctx);
    drawMessages(ctx);
    // The editor can replace a type without changing the count: it bumps appliedCount().
    if (rowsDirty_ || knownTypeCount_ != ctx.fixtures.size() || knownEditorApplies_ != FixtureEditRequests::appliedCount())
        rebuildRows(ctx);
    drawList(ctx);
    drawDetails(ctx);
}

// ---------------------------------------------------------------------------
// Toolbar and messages

void FixtureLibraryPanel::drawToolbar(EditorContext& ctx) {
    (void)ctx;
    const float buttonWidth = ImGui::CalcTextSize("Import...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::InputTextWithHint("##search", "Search fixtures", search_, sizeof(search_))) rowsDirty_ = true;
    ImGui::SameLine();

    ImGui::BeginDisabled(!FileDialogs::available() || dialogs_.busy());
    if (ImGui::Button("Import...")) {
        dialogs_.requestOpen("Import fixture", lastImportDir_,
                             {"Fixture files (OFL, GDTF)", "*.json *.gdtf", "All files", "*"});
    }
    ImGui::EndDisabled();
    if (!FileDialogs::available())
        ImGui::SetItemTooltip("No file dialog available (on Linux install zenity or kdialog).\n"
                              "You can also drop a fixture file onto the window.");
    else
        ImGui::SetItemTooltip("Import an Open Fixture Library .json or a GDTF file");
}

void FixtureLibraryPanel::drawMessages(EditorContext& ctx) {
    if (!importMessage_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, importFailed_ ? errorColor() : ImVec4(0.55f, 0.85f, 0.55f, 1.0f));
        ImGui::TextWrapped("%s", importMessage_.c_str());
        ImGui::PopStyleColor();
        for (const std::string& warning : importWarnings_) ImGui::TextDisabled("  warning: %s", warning.c_str());
    }

    const auto& errors = ctx.fixtures.errors();
    if (errors.empty()) return;
    char header[64];
    std::snprintf(header, sizeof(header), "Load problems (%d)###loadProblems", static_cast<int>(errors.size()));
    if (ImGui::CollapsingHeader(header)) {
        for (const auto& error : errors) {
            ImGui::PushStyleColor(ImGuiCol_Text, errorColor());
            ImGui::TextWrapped("%s: %s", error.path.filename().string().c_str(), error.message.c_str());
            ImGui::PopStyleColor();
        }
        if (ImGui::SmallButton("Clear problems")) ctx.fixtures.clearErrors();
    }
}

// ---------------------------------------------------------------------------
// List

void FixtureLibraryPanel::rebuildRows(EditorContext& ctx) {
    appliedSearch_ = search_;
    knownTypeCount_ = ctx.fixtures.size();
    knownEditorApplies_ = FixtureEditRequests::appliedCount();
    rowsDirty_ = false;

    const std::vector<const fixtures::FixtureType*> found = ctx.fixtures.search(appliedSearch_);
    const bool searching = !appliedSearch_.empty();
    rows_.clear();
    for (std::size_t i = 0; i < found.size();) {
        std::size_t end = i;
        while (end < found.size() && found[end]->manufacturer == found[i]->manufacturer) ++end;
        rows_.push_back({found[i], static_cast<int>(end - i)});
        // While searching every group is open, so a match is never hidden.
        if (searching || foldedManufacturers_.count(found[i]->manufacturer) == 0)
            for (std::size_t k = i; k < end; ++k) rows_.push_back({found[k], 0});
        i = end;
    }
}

void FixtureLibraryPanel::drawList(EditorContext& ctx) {
    const float detailHeight = ImGui::GetTextLineHeightWithSpacing() * kDetailLines + ImGui::GetStyle().WindowPadding.y * 2.0f;
    const float listHeight = std::max(60.0f, ImGui::GetContentRegionAvail().y - detailHeight - ImGui::GetStyle().ItemSpacing.y);
    if (!ImGui::BeginChild("##fixtureList", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return;
    }

    if (rows_.empty()) {
        ImGui::TextDisabled(ctx.fixtures.size() == 0 ? "The fixture library is empty. Use Import..."
                                                      : "No fixture matches the search.");
    }
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows_.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            ImGui::PushID(i);
            drawRow(ctx, rows_[static_cast<std::size_t>(i)]);
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

void FixtureLibraryPanel::drawRow(EditorContext& ctx, const Row& row) {
    const fixtures::FixtureType& type = *row.type;

    if (row.groupSize > 0) {  // manufacturer header
        const bool folded = foldedManufacturers_.count(type.manufacturer) != 0 && appliedSearch_.empty();
        char label[160];
        std::snprintf(label, sizeof(label), "%s %s  (%d)", folded ? "[+]" : "[-]",
                      type.manufacturer.empty() ? "(no manufacturer)" : type.manufacturer.c_str(), row.groupSize);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.70f, 0.35f, 1.0f));
        const bool clicked = ImGui::Selectable(label, false);
        ImGui::PopStyleColor();
        if (clicked && appliedSearch_.empty()) {
            if (folded)
                foldedManufacturers_.erase(type.manufacturer);
            else
                foldedManufacturers_.insert(type.manufacturer);
            rowsDirty_ = true;
        }
        return;
    }

    const bool selected = type.id == selectedId_;
    ImGui::Indent();
    const ImVec2 rowStart = ImGui::GetCursorScreenPos();
    const float rowRight = rowStart.x + ImGui::GetContentRegionAvail().x;
    if (ImGui::Selectable(type.name.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
        select(type);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            FixtureSpawner(ctx.scene, ctx.commands, ctx.selection, ctx.fixtures).addNearSelection(type.id, chosenMode(type));
    }
    ImGui::Unindent();

    // Drag the fixture onto the viewport; the drag preview names it.
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        FixtureDragPayload payload;
        payload.set(type.id, chosenMode(type));
        ImGui::SetDragDropPayload(kFixtureDragPayload, &payload, sizeof(payload));
        ImGui::Text("%s", type.name.c_str());
        ImGui::TextDisabled("Drop on a truss to hang it, anywhere else to place it");
        ImGui::EndDragDropSource();
    }

    // Mode summary at the right edge when the name leaves room for it; otherwise in the tooltip.
    char summary[48];
    formatModeSummary(type, summary, sizeof(summary));
    const float summaryX = rowRight - ImGui::CalcTextSize(summary).x - 6.0f;
    if (rowStart.x + ImGui::CalcTextSize(type.name.c_str()).x + 12.0f < summaryX)
        ImGui::GetWindowDrawList()->AddText(ImVec2(summaryX, rowStart.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), summary);
    else
        ImGui::SetItemTooltip("%s", summary);
}

void FixtureLibraryPanel::select(const fixtures::FixtureType& type) {
    if (type.id == selectedId_) return;
    selectedId_ = type.id;
    selectedMode_ = 0;
    selectedCategories_.clear();
    for (const std::string& category : type.categories) {
        if (!selectedCategories_.empty()) selectedCategories_ += ", ";
        selectedCategories_ += category;
    }
}

std::string FixtureLibraryPanel::chosenMode(const fixtures::FixtureType& type) const {
    if (type.id != selectedId_ || selectedMode_ < 0 || selectedMode_ >= static_cast<int>(type.modes.size())) {
        return type.modes.empty() ? std::string() : type.modes.front().name;
    }
    return type.modes[static_cast<std::size_t>(selectedMode_)].name;
}

// ---------------------------------------------------------------------------
// Details of the selected type

void FixtureLibraryPanel::drawDetails(EditorContext& ctx) {
    const fixtures::FixtureType* type = ctx.fixtures.find(selectedId_);
    if (!ImGui::BeginChild("##fixtureDetails", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return;
    }
    if (type == nullptr) {
        ImGui::TextDisabled("Select a fixture, then drag it onto the stage.");
        ImGui::EndChild();
        return;
    }

    ImGui::TextUnformatted(type->displayName().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("[%s]", std::string(fixtures::fixtureSourceName(type->source)).c_str());
    if (!selectedCategories_.empty()) ImGui::TextDisabled("%s", selectedCategories_.c_str());

    if (!type->modes.empty()) {
        selectedMode_ = std::clamp(selectedMode_, 0, static_cast<int>(type->modes.size()) - 1);
        const fixtures::DmxMode& current = type->modes[static_cast<std::size_t>(selectedMode_)];
        char preview[160];
        std::snprintf(preview, sizeof(preview), "%s (%d ch)", current.name.c_str(), current.footprint);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
        if (ImGui::BeginCombo("Mode", preview)) {
            for (int i = 0; i < static_cast<int>(type->modes.size()); ++i) {
                const fixtures::DmxMode& mode = type->modes[static_cast<std::size_t>(i)];
                char label[160];
                std::snprintf(label, sizeof(label), "%s (%d ch)", mode.name.c_str(), mode.footprint);
                if (ImGui::Selectable(label, i == selectedMode_)) selectedMode_ = i;
            }
            ImGui::EndCombo();
        }
    }
    ImGui::TextDisabled("%d beam(s), %.0f W", type->beamCount(), static_cast<double>(type->physical.power));

    ImGui::BeginDisabled(type->modes.empty());
    if (ImGui::Button("Add to scene")) FixtureSpawner(ctx.scene, ctx.commands, ctx.selection, ctx.fixtures).addNearSelection(type->id, chosenMode(*type));
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Hangs on the selected truss, or appears above the stage. It is patched to the next free address.");
    ImGui::SameLine();
    if (ImGui::Button("Edit")) {
        FixtureEditRequests::request(type->id);
        ImGui::SetWindowFocus(kFixtureEditorTitle);  // bring the editor tab to the front
    }
    ImGui::SetItemTooltip("Open a copy of this fixture in the Fixture Editor");
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Import

void FixtureLibraryPanel::pollImportDialog(EditorContext& ctx) {
    const std::optional<FileDialogs::Result> result = dialogs_.poll();
    if (!result || result->path.empty()) return;
    lastImportDir_ = result->path.parent_path();
    importFile(ctx, result->path);
}

void FixtureLibraryPanel::importFile(EditorContext& ctx, const std::filesystem::path& file) {
    std::string error;
    importWarnings_.clear();
    const std::optional<std::string> id = ctx.fixtures.importFile(file, &error, &importWarnings_);
    const std::string fileName = file.filename().string();
    if (!id) {
        importFailed_ = true;
        importMessage_ = "Cannot import " + fileName + ": " + error;
        log::warn("ui", "{}", importMessage_);
        return;
    }
    importFailed_ = false;
    importMessage_ = "Imported " + fileName + " as " + *id;
    log::info("ui", "{}", importMessage_);

    // Make the new type visible: clear the search and select it.
    search_[0] = '\0';
    rowsDirty_ = true;
    if (const fixtures::FixtureType* type = ctx.fixtures.find(*id)) {
        foldedManufacturers_.erase(type->manufacturer);
        selectedId_.clear();
        select(*type);
    }
}

}  // namespace dmxviz::ui
