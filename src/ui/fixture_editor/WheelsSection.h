#pragma once
// WheelsSection: the "Wheels" tab of the fixture editor.
//
// A list of wheels on the left; the selected wheel's slots on the right, as a table:
//   Open ........ nothing to edit
//   Color ....... colour picker (sRGB on screen, linear in the fixture)
//   Gobo ........ image from the fixture's resources (with a thumbnail) or a new PNG / JPG / SVG file
//   Prism ....... facet pattern: count + spread to generate circular or linear facets, or edit the list
//   Animation ... image, like a gobo
//   Frost ....... amount of diffusion
// Images are stored inside the fixture, so it stays one self-contained file.

#include "ui/FileDialogs.h"
#include "ui/fixture_editor/EditDocument.h"
#include "ui/fixture_editor/EditorWidgets.h"
#include "ui/fixture_editor/GoboThumbnails.h"

#include <filesystem>
#include <string>

namespace dmxviz::ui::fixture_editor {

class WheelsSection {
public:
    // Returns true when the fixture type changed.
    bool draw(EditDocument& doc);

    // Selects a wheel (jump from the validation list).
    void select(int wheelIndex) { selectedWheel_ = wheelIndex; }
    void reset();

private:
    // Structure changes are queued while the tables are drawn and run afterwards.
    enum class Action { None, RemoveSlot, SlotUp, SlotDown };

    bool drawWheelList(fixtures::FixtureType& type);
    bool drawWheel(fixtures::FixtureType& type, std::size_t wheelIndex);
    bool drawSlotRow(fixtures::FixtureType& type, fixtures::Wheel& wheel, std::size_t slotIndex);
    bool drawImageSlot(fixtures::FixtureType& type, fixtures::WheelSlot& slot, std::size_t slotIndex);
    bool drawPrismSlot(fixtures::WheelSlot& slot);
    bool runSlotAction(fixtures::Wheel& wheel);
    void pollImageImport(fixtures::FixtureType& type, bool& changed);

    int selectedWheel_ = 0;

    // Name field of the selected wheel: applied when the user leaves the field.
    std::string nameEdit_;
    int nameEditWheel_ = -1;
    std::string nameEditSource_;

    Action pending_ = Action::None;
    std::size_t pendingSlot_ = 0;

    // Prism generator settings (shared by all prism slots; they are only starting values).
    int facetCount_ = 3;
    float facetSpreadDegrees_ = 6.0f;
    int facetPattern_ = 0;  // 0 circular, 1 linear

    GoboThumbnails thumbnails_;
    FileDialogs dialogs_;
    int importWheel_ = -1;
    int importSlot_ = -1;
    std::filesystem::path lastImportDir_;
    std::string message_;
};

}  // namespace dmxviz::ui::fixture_editor
