#pragma once
// FixtureEditorPanel: create and edit fixture types (FR-FIX-1..4).
//
// The editor works on a *copy* of a fixture type: "Edit" in the Fixture Library panel, "Open"
// here, or "New" from a template (blank, LED par, moving head). Nothing reaches the library until
// "Apply" (which adds the fixture or replaces the one with the same id); "Revert" goes back to the
// state at open / last apply; "Save as file..." writes a native *.dmxviz-fixture.json.
//
// Tabs, one section class each (src/ui/fixture_editor/):
//   General ........ identity, physical data, movement limits
//   Geometry ....... node tree, models, beams, "make cells"
//   Wheels ......... colour / gobo / prism / animation / frost wheels
//   Modes & Channels  DMX modes, channel table, function table
//   Validation ..... problems found by FixtureValidator; errors block Apply
// and a live 3D preview next to the tabs (or as a tab when the window is narrow).

#include "ui/FileDialogs.h"
#include "ui/Panel.h"
#include "ui/fixture_editor/EditDocument.h"
#include "ui/fixture_editor/FixtureValidation.h"
#include "ui/fixture_editor/GeneralSection.h"
#include "ui/fixture_editor/GeometrySection.h"
#include "ui/fixture_editor/ModesSection.h"
#include "ui/fixture_editor/PreviewSection.h"
#include "ui/fixture_editor/ValidationSection.h"
#include "ui/fixture_editor/WheelsSection.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dmxviz::ui {

class FixtureEditorPanel : public Panel {
public:
    FixtureEditorPanel();
    ~FixtureEditorPanel() override;
    FixtureEditorPanel(const FixtureEditorPanel&) = delete;
    FixtureEditorPanel& operator=(const FixtureEditorPanel&) = delete;

    const char* title() const override;
    void draw(EditorContext& ctx) override;

private:
    enum class Tab { General, Geometry, Wheels, Modes, Validation, Preview, None };

    // Something that replaces the open document; asks for confirmation when there are unapplied edits.
    struct OpenRequest {
        enum class Kind { None, Copy, New, Close } kind = Kind::None;
        std::string fixtureId;
        fixture_editor::FixtureTemplate tmpl = fixture_editor::FixtureTemplate::Blank;
    };

    void drawToolbar(EditorContext& ctx);
    void drawNewMenu(EditorContext& ctx);
    void drawOpenMenu(EditorContext& ctx);
    void drawHeader(EditorContext& ctx);
    void drawStartScreen(EditorContext& ctx);
    void drawBody(EditorContext& ctx);
    void drawTabs(EditorContext& ctx, bool previewAsTab);
    void drawDiscardPopup(EditorContext& ctx);

    void requestOpen(EditorContext& ctx, OpenRequest request);
    void openNow(EditorContext& ctx, const OpenRequest& request);
    void applyToLibrary(EditorContext& ctx);
    void pollSaveDialog();
    void requestSave();
    void revalidate(EditorContext& ctx);
    void jumpTo(const fixture_editor::Problem& problem);
    void setStatus(std::string text, bool error);

    // True when another library type uses `id` (the type this document started from does not count).
    fixture_editor::IdTakenFn idTakenFn(EditorContext& ctx) const;
    // Calls doc_.touch() with the library-aware id check.
    void touch(EditorContext& ctx);
    int currentMode() const { return modes_.selectedMode(); }

    fixture_editor::EditDocument doc_;
    fixture_editor::GeneralSection general_;
    fixture_editor::GeometrySection geometry_;
    fixture_editor::WheelsSection wheels_;
    fixture_editor::ModesSection modes_;
    fixture_editor::ValidationSection validation_;
    fixture_editor::PreviewSection preview_;

    std::vector<fixture_editor::Problem> problems_;
    fixture_editor::ProblemCounts counts_;
    std::uint64_t validatedRevision_ = ~0ull;
    std::size_t validatedLibrarySize_ = 0;

    Tab requestedTab_ = Tab::None;  // tab to switch to in the next frame
    OpenRequest pendingOpen_;
    bool discardPopupRequested_ = false;

    FileDialogs dialogs_;
    std::filesystem::path lastSaveDir_;

    std::string status_;
    bool statusIsError_ = false;
    char openFilter_[64] = {};
};

}  // namespace dmxviz::ui
