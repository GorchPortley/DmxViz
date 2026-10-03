#pragma once
// GeneralSection: the "General" tab of the fixture editor.
//
// Identity (manufacturer, name, short name, id, description), physical data (weight, power,
// dimensions, dimmer curve) and the movement limits: pan/tilt range (edited in the Pan and
// Tilt channel functions, in every mode) and speeds.

#include "ui/fixture_editor/EditDocument.h"

namespace dmxviz::ui::fixture_editor {

class GeneralSection {
public:
    // Returns true when anything changed (the caller bumps the document revision).
    bool draw(EditDocument& doc, const IdTakenFn& idTaken);

private:
    bool drawIdentity(EditDocument& doc, const IdTakenFn& idTaken);
    bool drawPhysical(fixtures::PhysicalSpec& physical);
    bool drawMovement(fixtures::FixtureType& type);
    // One "range" row for Pan or Tilt. `label` is also the id.
    bool drawAxisRange(fixtures::FixtureType& type, fixtures::Attribute attribute, const char* label);
};

}  // namespace dmxviz::ui::fixture_editor
