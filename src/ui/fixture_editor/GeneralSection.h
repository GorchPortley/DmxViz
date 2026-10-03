#pragma once
// GeneralSection: the "General" tab of the fixture editor.
//
// Identity (manufacturer, name, short name, id, description, categories, revision), physical data
// (weight, power, dimensions, dimmer curve), the movement limits (pan/tilt range, edited in the Pan
// and Tilt channel functions of every mode, and speeds) and the LED emitters.

#include "ui/fixture_editor/EditDocument.h"
#include "ui/fixture_editor/EditorWidgets.h"

#include <string>

namespace dmxviz::ui::fixture_editor {

class GeneralSection {
public:
    // Returns true when anything changed (the caller bumps the document revision).
    bool draw(EditDocument& doc, const IdTakenFn& idTaken);

private:
    bool drawIdentity(EditDocument& doc, const IdTakenFn& idTaken);
    bool drawPhysical(fixtures::PhysicalSpec& physical);
    bool drawMovement(fixtures::FixtureType& type);
    bool drawEmitters(fixtures::FixtureType& type);
    // One "range" row for Pan or Tilt. `label` is also the id.
    bool drawAxisRange(fixtures::FixtureType& type, fixtures::Attribute attribute, const char* label);

    // Categories are edited as one comma separated line.
    std::string categoriesText_;
    std::string categoriesSource_;
    RenameField emitterName_;
};

}  // namespace dmxviz::ui::fixture_editor
