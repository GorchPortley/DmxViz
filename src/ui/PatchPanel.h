#pragma once
// PatchPanel: the patch spreadsheet - one row per fixture node with its fixture number,
// name, type, DMX mode, universe, address, footprint and end address.
//
//   * fields are edited in place; every edit is an undoable command;
//   * rows whose slots overlap another fixture (or run past slot 512) are red, the
//     tooltip says with whom;
//   * click a column header to sort; click a row to select the fixture in the scene;
//   * "Auto-patch selected" gives the selected fixtures consecutive addresses.

#include "core/Id.h"
#include "fixtures/FixtureType.h"
#include "stage/Node.h"
#include "ui/Panel.h"
#include "ui/PatchModel.h"

#include <cstdint>
#include <vector>

struct ImGuiTableSortSpecs;

namespace dmxviz::ui {

class PatchPanel : public Panel {
public:
    const char* title() const override;
    void draw(EditorContext& ctx) override;

private:
    // One fixture node. The pointers are only valid within the frame they were collected in
    // (the scene may be replaced between frames), so rows are rebuilt every frame.
    struct Row {
        NodeId id = kInvalidNode;
        const stage::Node* node = nullptr;
        const stage::FixtureContent* fixture = nullptr;
        const fixtures::FixtureType* type = nullptr;
        const fixtures::DmxMode* mode = nullptr;
        int footprint = 0;
        PatchProblem problem = PatchProblem::None;
    };
    enum Column { kNumber, kName, kType, kMode, kUniverse, kAddress, kFootprint, kEnd, kColumnCount };

    void collectRows(EditorContext& ctx);
    void sortRows(const ImGuiTableSortSpecs* specs);
    void findProblems();

    void drawToolbar(EditorContext& ctx);
    void drawTable(EditorContext& ctx);
    void drawRow(EditorContext& ctx, const Row& row, int rowIndex);
    void drawProblemTooltip(const Row& row, int rowIndex) const;

    void autoPatchSelected(EditorContext& ctx);
    void unpatchSelected(EditorContext& ctx);
    // Applies `edit` to the fixture content of one node as an undoable command.
    template <typename Edit>
    void editFixture(EditorContext& ctx, NodeId id, const char* label, Edit&& edit);

    std::vector<Row> rows_;
    std::vector<PatchSlot> slots_;  // parallel to rows_, input of the conflict check
    std::vector<PatchProblem> problems_;
    std::vector<int> problemScratch_;
    int conflictCount_ = 0;

    // Auto-patch settings.
    int startUniverse_ = 1;
    int startAddress_ = 1;
    bool avoidOthers_ = true;

    std::uint64_t lastSelectionRevision_ = 0;
    bool selectionChangedHere_ = false;
    bool scrollToPrimary_ = false;
};

}  // namespace dmxviz::ui
