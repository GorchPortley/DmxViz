#pragma once
// FixtureLibraryPanel: browse and search the fixture library, import OFL / GDTF files,
// and put fixtures on the stage.
//
//   * the list is grouped by manufacturer (click a group to fold it);
//   * drag a fixture onto the viewport (see ViewportPanel) or double-click / press
//     "Add to scene" - the new fixture is patched to the next free address;
//   * "Import..." opens a native file dialog for *.json (OFL or native) and *.gdtf.

#include "fixtures/FixtureType.h"
#include "ui/FileDialogs.h"
#include "ui/Panel.h"

#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace dmxviz::ui {

class FixtureLibraryPanel : public Panel {
public:
    const char* title() const override;
    void draw(EditorContext& ctx) override;

private:
    // One line of the list: a manufacturer header (groupSize > 0) or a fixture type.
    struct Row {
        const fixtures::FixtureType* type = nullptr;  // for a header: the group's first type
        int groupSize = 0;
    };

    void drawToolbar(EditorContext& ctx);
    void drawMessages(EditorContext& ctx);
    void drawList(EditorContext& ctx);
    void drawRow(EditorContext& ctx, const Row& row);
    void drawDetails(EditorContext& ctx);

    void rebuildRows(EditorContext& ctx);
    void pollImportDialog(EditorContext& ctx);
    void importFile(EditorContext& ctx, const std::filesystem::path& file);
    void select(const fixtures::FixtureType& type);
    // Name of the mode a drag or "Add" of this type should use (the selected one, else the first).
    std::string chosenMode(const fixtures::FixtureType& type) const;

    FileDialogs dialogs_;
    std::filesystem::path lastImportDir_;

    char search_[128] = {};
    std::string appliedSearch_;
    std::size_t knownTypeCount_ = static_cast<std::size_t>(-1);
    std::uint64_t knownEditorApplies_ = static_cast<std::uint64_t>(-1);
    bool rowsDirty_ = true;
    std::vector<Row> rows_;
    std::set<std::string> foldedManufacturers_;

    std::string selectedId_;
    int selectedMode_ = 0;
    std::string selectedCategories_;

    // Result of the last import.
    std::string importMessage_;
    bool importFailed_ = false;
    std::vector<std::string> importWarnings_;
};

}  // namespace dmxviz::ui
