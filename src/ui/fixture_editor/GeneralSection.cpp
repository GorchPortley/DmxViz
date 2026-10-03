#include "ui/fixture_editor/GeneralSection.h"

#include "ui/fixture_editor/ChannelEditing.h"
#include "ui/fixture_editor/EditorWidgets.h"

#include "imgui.h"
#include "imgui_stdlib.h"

namespace dmxviz::ui::fixture_editor {

bool GeneralSection::draw(EditDocument& doc, const IdTakenFn& idTaken) {
    bool changed = false;
    fixtures::FixtureType& type = doc.type();

    ImGui::SeparatorText("Identity");
    changed |= drawIdentity(doc, idTaken);

    ImGui::SeparatorText("Physical");
    changed |= drawPhysical(type.physical);

    ImGui::SeparatorText("Movement");
    changed |= drawMovement(type);
    return changed;
}

bool GeneralSection::drawIdentity(EditDocument& doc, const IdTakenFn& idTaken) {
    bool changed = false;
    fixtures::FixtureType& type = doc.type();

    changed |= inputText("Manufacturer", type.manufacturer);
    changed |= inputText("Name", type.name);
    changed |= inputText("Short name", type.shortName);

    // The id is the library key: it follows manufacturer + name until the user types their own.
    const bool idProblem = type.id.empty() || (idTaken && idTaken(type.id));
    ImGui::BeginDisabled(doc.autoId());
    if (idProblem) ImGui::PushStyleColor(ImGuiCol_Text, errorColor());
    changed |= inputText("Id", type.id);
    if (idProblem) ImGui::PopStyleColor();
    ImGui::EndDisabled();
    bool autoId = doc.autoId();
    if (ImGui::Checkbox("Generate id from manufacturer and name", &autoId)) {
        doc.setAutoId(autoId, idTaken);
        changed = true;
    }
    if (type.id.empty()) ImGui::TextColored(errorColor(), "The id must not be empty.");
    else if (idTaken && idTaken(type.id)) ImGui::TextColored(errorColor(), "Another fixture already uses this id.");
    else if (!doc.isNew() && type.id != doc.originalId())
        ImGui::TextDisabled("Apply will add a new fixture; \"%s\" stays as it is.", doc.originalId().c_str());
    else if (doc.isNew()) ImGui::TextDisabled("Apply will add this fixture to the library.");
    else ImGui::TextDisabled("Apply will replace \"%s\" in the library.", doc.originalId().c_str());

    changed |= ImGui::InputTextMultiline("Description", &type.description, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.5f));
    return changed;
}

bool GeneralSection::drawPhysical(fixtures::PhysicalSpec& physical) {
    bool changed = false;
    changed |= ImGui::DragFloat("Weight", &physical.weight, 0.1f, 0.0f, 1000.0f, "%.1f kg");
    changed |= ImGui::DragFloat("Power", &physical.power, 1.0f, 0.0f, 20000.0f, "%.0f W");
    changed |= dragVec3("Size (W x H x D)", physical.dimensions, 0.005f, 0.0f, 20.0f, "%.3f m");
    int curve = static_cast<int>(physical.dimmerCurve);
    static constexpr const char* kCurves[] = {"Linear", "Square law"};
    if (comboIndex("Dimmer curve", curve, kCurves)) {
        physical.dimmerCurve = static_cast<fixtures::DimmerCurve>(curve);
        changed = true;
    }
    return changed;
}

bool GeneralSection::drawAxisRange(fixtures::FixtureType& type, fixtures::Attribute attribute, const char* label) {
    const std::optional<PhysicalRange> range = attributeRange(type, attribute);
    ImGui::PushID(label);
    bool changed = false;
    if (!range) {
        ImGui::BeginDisabled();
        float none[2] = {0.0f, 0.0f};
        ImGui::DragFloat2(label, none);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("no %s channel", label);
    } else {
        float degrees[2] = {radToDeg(range->from), radToDeg(range->to)};
        if (ImGui::DragFloat2(label, degrees, 1.0f, -1440.0f, 1440.0f, "%.0f deg")) {
            setAttributeRange(type, attribute, degToRad(degrees[0]), degToRad(degrees[1]));
            changed = true;
        }
    }
    ImGui::PopID();
    return changed;
}

bool GeneralSection::drawMovement(fixtures::FixtureType& type) {
    bool changed = false;
    changed |= drawAxisRange(type, fixtures::Attribute::Pan, "Pan range");
    changed |= drawAxisRange(type, fixtures::Attribute::Tilt, "Tilt range");
    helpMarker("The range is the physical angle of the Pan and Tilt channel functions, in every mode: "
               "DMX 0 gives the first angle, the maximum DMX value the second.");

    fixtures::MovementSpec& m = type.physical.movement;
    changed |= dragDegrees("Pan max speed", m.panMaxSpeed, 1.0f, 1.0f, 3600.0f, "%.0f deg/s");
    changed |= dragDegrees("Tilt max speed", m.tiltMaxSpeed, 1.0f, 1.0f, 3600.0f, "%.0f deg/s");
    changed |= dragDegrees("Pan acceleration", m.panAcceleration, 5.0f, 10.0f, 20000.0f, "%.0f deg/s2");
    changed |= dragDegrees("Tilt acceleration", m.tiltAcceleration, 5.0f, 10.0f, 20000.0f, "%.0f deg/s2");
    changed |= ImGui::DragFloat("Wheel speed", &m.wheelSlotsPerSecond, 0.1f, 0.5f, 100.0f, "%.1f slots/s");
    changed |= dragDegrees("Index rotation speed", m.indexRotationSpeed, 5.0f, 10.0f, 7200.0f, "%.0f deg/s");
    return changed;
}

}  // namespace dmxviz::ui::fixture_editor
