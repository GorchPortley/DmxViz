#pragma once
// EditorWidgets: small ImGui helpers shared by the fixture editor's sections.
//
// All of them return true when the user changed the value this frame, so a section can write
// `changed |= widget(...)` and mark the document as modified once.

#include "core/Math.h"
#include "fixtures/Attribute.h"

#include "imgui.h"

#include <span>
#include <string>

namespace dmxviz::ui::fixture_editor {

// Colours used for problems and hints.
ImVec4 errorColor();
ImVec4 warningColor();
ImVec4 okColor();

// A text field that edits a std::string.
bool inputText(const char* label, std::string& text, ImGuiInputTextFlags flags = 0);

// DragFloat for a value stored in radians and shown in degrees.
bool dragDegrees(const char* label, float& radians, float speed = 0.5f, float minDegrees = -360.0f,
                 float maxDegrees = 360.0f, const char* format = "%.1f deg");

// DragFloat3 with a unit suffix in the format, e.g. "%.3f m".
bool dragVec3(const char* label, glm::vec3& value, float speed, float min, float max, const char* format);

// Combo over a list of names; `index` is the selected entry.
bool comboIndex(const char* label, int& index, std::span<const char* const> names);

// Combo of the attributes with a search box. Create one picker per place that needs it and call
// draw() every frame; only one popup is open at a time, so sharing one is fine too.
class AttributePicker {
public:
    // `value` is shown as the preview text; returns true when another attribute was picked.
    bool draw(const char* label, fixtures::Attribute& value, float width = 0.0f);

private:
    std::string filter_;
};

// A small "?" with a tooltip, for hints that would clutter the layout.
void helpMarker(const char* text);

}  // namespace dmxviz::ui::fixture_editor
