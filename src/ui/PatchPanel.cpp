#include "ui/PatchPanel.h"

#include "core/Log.h"
#include "stage/Commands.h"
#include "ui/EditorContext.h"
#include "ui/PanelTitles.h"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <compare>
#include <cstdio>
#include <cstring>
#include <functional>
#include <utility>

namespace dmxviz::ui {

namespace {

constexpr ImU32 kConflictRow = IM_COL32(150, 35, 35, 120);
constexpr ImU32 kConflictText = IM_COL32(255, 120, 110, 255);

// The command merge is closed when an input loses focus, so editing the same field twice
// later makes two undo steps while typing or dragging within one edit makes one.
void finishEdit(EditorContext& ctx) {
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.commands.breakMerge();
}

}  // namespace

const char* PatchPanel::title() const {
    return kPatchTitle;
}

template <typename Edit>
void PatchPanel::editFixture(EditorContext& ctx, NodeId id, const char* label, Edit&& edit) {
    ctx.commands.execute(stage::makeEditCommand(
        ctx.scene, {id}, label,
        [&edit](stage::NodeData& data) {
            if (stage::FixtureContent* fixture = data.as<stage::FixtureContent>()) edit(*fixture);
        },
        label));
}

// ---------------------------------------------------------------------------
// Rows

void PatchPanel::collectRows(EditorContext& ctx) {
    rows_.clear();
    for (const NodeId id : ctx.scene.nodesOfKind(stage::NodeKind::Fixture)) {
        const stage::Node* node = ctx.scene.find(id);
        const stage::FixtureContent* fixture = node != nullptr ? node->as<stage::FixtureContent>() : nullptr;
        if (fixture == nullptr) continue;
        Row row;
        row.id = id;
        row.node = node;
        row.fixture = fixture;
        row.type = ctx.fixtures.find(fixture->fixtureTypeId);
        row.mode = row.type != nullptr ? fixtureMode(*row.type, *fixture) : nullptr;
        row.footprint = row.mode != nullptr ? row.mode->footprint : 0;
        rows_.push_back(row);
    }
}

void PatchPanel::sortRows(const ImGuiTableSortSpecs* specs) {
    if (specs == nullptr || specs->SpecsCount == 0) return;  // no sort: keep scene order
    const ImGuiTableColumnSortSpecs spec = specs->Specs[0];
    const bool ascending = spec.SortDirection == ImGuiSortDirection_Ascending;

    const auto compare = [&spec](const Row& a, const Row& b) {
        const auto textOf = [](const Row& r, int column) -> const char* {
            if (column == kName) return r.node->name().c_str();
            if (column == kType) return r.type != nullptr ? r.type->name.c_str() : r.fixture->fixtureTypeId.c_str();
            return r.fixture->modeName.c_str();
        };
        switch (spec.ColumnUserID) {
            case kNumber:
                return static_cast<int>(a.fixture->fixtureNumber) <=> static_cast<int>(b.fixture->fixtureNumber);
            case kName:
            case kType:
            case kMode:
                return std::strcmp(textOf(a, static_cast<int>(spec.ColumnUserID)),
                                   textOf(b, static_cast<int>(spec.ColumnUserID))) <=> 0;
            case kUniverse:
                return a.fixture->patch.universe <=> b.fixture->patch.universe;
            case kAddress:
                return a.fixture->patch.address <=> b.fixture->patch.address;
            case kFootprint:
                return a.footprint <=> b.footprint;
            default:  // kEnd
                return (static_cast<int>(a.fixture->patch.address) + a.footprint) <=>
                       (static_cast<int>(b.fixture->patch.address) + b.footprint);
        }
    };
    // Equal keys fall back to the patch order, so the result is stable and tidy.
    std::stable_sort(rows_.begin(), rows_.end(), [&](const Row& a, const Row& b) {
        const auto order = compare(a, b);
        if (order != 0) return ascending ? order < 0 : order > 0;
        if (a.fixture->patch.universe != b.fixture->patch.universe)
            return a.fixture->patch.universe < b.fixture->patch.universe;
        return a.fixture->patch.address < b.fixture->patch.address;
    });
}

void PatchPanel::findProblems() {
    slots_.clear();
    for (const Row& row : rows_) slots_.push_back(PatchSlot{row.fixture->patch, row.footprint});
    findPatchProblems(slots_, problems_, problemScratch_);
    conflictCount_ = 0;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        rows_[i].problem = problems_[i];
        if (problems_[i] != PatchProblem::None) ++conflictCount_;
    }
}

// ---------------------------------------------------------------------------
// Drawing

void PatchPanel::draw(EditorContext& ctx) {
    // Remember whether the selection changed elsewhere, to bring its row into view.
    if (ctx.selection.revision() != lastSelectionRevision_) {
        lastSelectionRevision_ = ctx.selection.revision();
        scrollToPrimary_ = !selectionChangedHere_;
    }
    selectionChangedHere_ = false;

    collectRows(ctx);
    drawToolbar(ctx);
    drawTable(ctx);
}

void PatchPanel::drawToolbar(EditorContext& ctx) {
    // Counts come from the previous frame's conflict check; good enough for a status line.
    ImGui::Text("%d fixture%s", static_cast<int>(rows_.size()), rows_.size() == 1 ? "" : "s");
    ImGui::SameLine();
    if (conflictCount_ > 0)
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kConflictText), "%d with address problems", conflictCount_);
    else
        ImGui::TextDisabled("no address conflicts");

    // The auto-patch controls follow on the same line when there is room, otherwise they wrap.
    const auto sameLineIfRoom = [](float width) {
        if (ImGui::GetContentRegionAvail().x >= width) ImGui::SameLine(0.0f, 20.0f);
    };
    sameLineIfRoom(250.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Start");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(52.0f);
    ImGui::InputInt("##startUniverse", &startUniverse_, 0, 0);
    ImGui::SetItemTooltip("First universe for auto-patch");
    startUniverse_ = std::clamp(startUniverse_, 1, static_cast<int>(kMaxUniverse));
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::TextUnformatted(".");
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::SetNextItemWidth(52.0f);
    ImGui::InputInt("##startAddress", &startAddress_, 0, 0);
    ImGui::SetItemTooltip("First address for auto-patch");
    startAddress_ = std::clamp(startAddress_, 1, kDmxSlots);
    ImGui::SameLine();
    ImGui::Checkbox("Skip used", &avoidOthers_);
    ImGui::SetItemTooltip("Leave out addresses that other (unselected) fixtures already use");

    sameLineIfRoom(270.0f);
    ImGui::BeginDisabled(ctx.selection.empty());
    if (ImGui::Button("Auto-patch selected")) autoPatchSelected(ctx);
    ImGui::SetItemTooltip("Give the selected fixtures consecutive addresses from the start address,\n"
                          "in the order of the table. Fixtures never cross slot 512.");
    ImGui::SameLine();
    if (ImGui::Button("Unpatch selected")) unpatchSelected(ctx);
    ImGui::EndDisabled();
}

void PatchPanel::drawTable(EditorContext& ctx) {
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
                                      ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable |
                                      ImGuiTableFlags_SortTristate | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("##patch", kColumnCount, flags)) return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 56.0f, kNumber);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.4f, kName);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.4f, kType);
    ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthStretch, 1.4f, kMode);
    ImGui::TableSetupColumn("Univ", ImGuiTableColumnFlags_WidthFixed, 64.0f, kUniverse);
    ImGui::TableSetupColumn("Addr", ImGuiTableColumnFlags_WidthFixed, 64.0f, kAddress);
    ImGui::TableSetupColumn("Chans", ImGuiTableColumnFlags_WidthFixed, 48.0f, kFootprint);
    ImGui::TableSetupColumn("End", ImGuiTableColumnFlags_WidthFixed, 56.0f, kEnd);
    ImGui::TableHeadersRow();

    sortRows(ImGui::TableGetSortSpecs());
    findProblems();

    // Index of the primary selected row, to scroll to it once.
    int primaryIndex = -1;
    if (scrollToPrimary_) {
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].id == ctx.selection.primary()) primaryIndex = static_cast<int>(i);
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows_.size()));
    if (primaryIndex >= 0) clipper.IncludeItemByIndex(primaryIndex);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            // Edits replace the scene's data, so work on a copy of the row's small fields.
            const Row row = rows_[static_cast<std::size_t>(i)];
            ImGui::PushID(static_cast<int>(row.id));
            drawRow(ctx, row, i);
            if (i == primaryIndex) ImGui::SetScrollHereY(0.5f);
            ImGui::PopID();
        }
    }
    scrollToPrimary_ = false;  // one attempt per selection change
    ImGui::EndTable();
}

void PatchPanel::drawRow(EditorContext& ctx, const Row& row, int rowIndex) {
    const stage::FixtureContent& fixture = *row.fixture;
    const bool problem = row.problem != PatchProblem::None;
    ImGui::TableNextRow();
    if (problem) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kConflictRow);

    // Column 0: a selectable spanning the whole row; the fixture number is edited on top of it.
    ImGui::TableSetColumnIndex(kNumber);
    const bool selected = ctx.selection.contains(row.id);
    if (ImGui::Selectable("##select", selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl)
            ctx.selection.toggle(row.id);
        else if (io.KeyShift)
            ctx.selection.add(row.id);
        else
            ctx.selection.set(row.id);
        selectionChangedHere_ = true;
    }
    if (problem && ImGui::IsItemHovered()) drawProblemTooltip(row, rowIndex);
    ImGui::SameLine(0.0f, 0.0f);
    int number = fixture.fixtureNumber;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputInt("##number", &number, 0, 0, ImGuiInputTextFlags_AutoSelectAll)) {
        number = std::max(number, 0);
        editFixture(ctx, row.id, "Fixture number", [number](stage::FixtureContent& f) { f.fixtureNumber = number; });
    }
    finishEdit(ctx);

    // Name (a node property, not part of the fixture content).
    ImGui::TableSetColumnIndex(kName);
    char name[128];
    std::snprintf(name, sizeof(name), "%s", row.node->name().c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputText("##name", name, sizeof(name), ImGuiInputTextFlags_AutoSelectAll)) {
        const std::string newName = name;
        ctx.commands.execute(stage::makeEditCommand(
            ctx.scene, {row.id}, "Rename", [newName](stage::NodeData& data) { data.name = newName; }, "Rename"));
    }
    finishEdit(ctx);

    // Type: read-only (changing a type means replacing the fixture).
    ImGui::TableSetColumnIndex(kType);
    if (row.type != nullptr) {
        ImGui::TextUnformatted(row.type->displayName().c_str());
    } else {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kConflictText), "missing: %s", fixture.fixtureTypeId.c_str());
        ImGui::SetItemTooltip("This fixture type is not in the library. Import it to see this fixture.");
    }

    // Mode: combo of the type's modes. The footprint (and so the end address) follows.
    ImGui::TableSetColumnIndex(kMode);
    if (row.type != nullptr && row.mode != nullptr) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##mode", row.mode->name.c_str())) {
            for (const fixtures::DmxMode& mode : row.type->modes) {
                char label[160];
                std::snprintf(label, sizeof(label), "%s (%d ch)", mode.name.c_str(), mode.footprint);
                if (ImGui::Selectable(label, &mode == row.mode)) {
                    const std::string modeName = mode.name;
                    editFixture(ctx, row.id, "Change mode", [modeName](stage::FixtureContent& f) { f.modeName = modeName; });
                    ctx.commands.breakMerge();
                }
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::TextDisabled("%s", fixture.modeName.c_str());
    }

    // Universe and address: 0 means "not patched".
    ImGui::TableSetColumnIndex(kUniverse);
    int universe = static_cast<int>(fixture.patch.universe);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputInt("##universe", &universe, 0, 0, ImGuiInputTextFlags_AutoSelectAll)) {
        universe = std::clamp(universe, 0, static_cast<int>(kMaxUniverse));
        editFixture(ctx, row.id, "Patch universe", [universe](stage::FixtureContent& f) {
            f.patch.universe = static_cast<std::uint32_t>(universe);
        });
    }
    finishEdit(ctx);

    ImGui::TableSetColumnIndex(kAddress);
    int address = static_cast<int>(fixture.patch.address);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputInt("##address", &address, 0, 0, ImGuiInputTextFlags_AutoSelectAll)) {
        address = std::clamp(address, 0, kDmxSlots);
        editFixture(ctx, row.id, "Patch address", [address](stage::FixtureContent& f) {
            f.patch.address = static_cast<std::uint32_t>(address);
        });
    }
    finishEdit(ctx);

    ImGui::TableSetColumnIndex(kFootprint);
    if (row.footprint > 0)
        ImGui::Text("%d", row.footprint);
    else
        ImGui::TextDisabled("-");

    ImGui::TableSetColumnIndex(kEnd);
    const PatchSlot& slot = slots_[static_cast<std::size_t>(rowIndex)];
    if (!slot.patched()) {
        ImGui::TextDisabled("-");
    } else {
        if (problem) ImGui::PushStyleColor(ImGuiCol_Text, kConflictText);
        ImGui::Text("%d%s", slot.endAddress(), problem ? " !" : "");
        if (problem) ImGui::PopStyleColor();
        if (problem && ImGui::IsItemHovered()) drawProblemTooltip(row, rowIndex);
    }
}

void PatchPanel::drawProblemTooltip(const Row& row, int rowIndex) const {
    if (!ImGui::BeginTooltip()) return;
    const PatchSlot& slot = slots_[static_cast<std::size_t>(rowIndex)];
    if (row.problem == PatchProblem::Overflow || slot.endAddress() > kDmxSlots) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kConflictText),
                           "Runs past slot 512: %d channels from address %u end at %d.", row.footprint,
                           static_cast<unsigned>(slot.patch.address), slot.endAddress());
    }
    bool header = false;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (static_cast<int>(i) == rowIndex || !patchesOverlap(slot, slots_[i])) continue;
        if (!header) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kConflictText), "Overlaps with:");
        header = true;
        ImGui::BulletText("%s  (%u.%u - %d)", rows_[i].node->name().c_str(),
                          static_cast<unsigned>(slots_[i].patch.universe),
                          static_cast<unsigned>(slots_[i].patch.address), slots_[i].endAddress());
    }
    ImGui::EndTooltip();
}

// ---------------------------------------------------------------------------
// Bulk actions

void PatchPanel::autoPatchSelected(EditorContext& ctx) {
    // Selected fixtures in the order the table shows them.
    std::vector<NodeId> ids;
    std::vector<int> footprints;
    for (const Row& row : rows_) {
        if (!ctx.selection.contains(row.id)) continue;
        ids.push_back(row.id);
        footprints.push_back(row.footprint);
    }
    if (ids.empty()) return;

    const stage::DmxPatch start{static_cast<std::uint32_t>(startUniverse_), static_cast<std::uint32_t>(startAddress_)};
    const std::vector<stage::DmxPatch> plan =
        planAutoPatch(footprints, start, avoidOthers_ ? PatchAllocator(ctx.scene, ctx.fixtures, ids) : PatchAllocator());

    std::vector<std::pair<NodeId, stage::NodeData>> after;
    int unplaced = 0;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        stage::NodeData data = ctx.scene.find(ids[i])->data();
        stage::FixtureContent* fixture = data.as<stage::FixtureContent>();
        if (fixture == nullptr) continue;
        if (!plan[i].patched()) ++unplaced;
        fixture->patch = plan[i];
        after.emplace_back(ids[i], std::move(data));
    }
    ctx.commands.execute(std::make_unique<stage::EditNodesCommand>(std::move(after), "Auto-patch"));
    ctx.commands.breakMerge();
    log::info("ui", "auto-patched {} fixture(s) from {}.{}{}", ids.size() - static_cast<std::size_t>(unplaced),
              startUniverse_, startAddress_, unplaced > 0 ? " (some could not be placed)" : "");
}

void PatchPanel::unpatchSelected(EditorContext& ctx) {
    std::vector<NodeId> ids;
    for (const Row& row : rows_)
        if (ctx.selection.contains(row.id)) ids.push_back(row.id);
    if (ids.empty()) return;
    ctx.commands.execute(stage::makeEditCommand(ctx.scene, ids, "Unpatch", [](stage::NodeData& data) {
        if (stage::FixtureContent* fixture = data.as<stage::FixtureContent>()) fixture->patch = {};
    }));
    ctx.commands.breakMerge();
}

}  // namespace dmxviz::ui
