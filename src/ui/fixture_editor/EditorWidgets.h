#pragma once
// EditorWidgets: small ImGui helpers shared by the fixture editor's sections.
//
// All of them return true when the user changed the value this frame, so a section can write
// `changed |= widget(...)` and mark the document as modified once.

#include "core/Math.h"
#include "fixtures/Attribute.h"

#include "imgui.h"

#include <optional>
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

// A text field for a name that other data refers to (emitters, groups). The user types freely and
// the new name is only handed back when the field is left, so a half-typed name never breaks a
// reference. One field object can serve many rows: it remembers which ImGui item is being edited.
class RenameField {
public:
    // Draws the field showing `current`. Returns the new text once editing finished and it differs
    // from `current` (the caller decides whether it is acceptable and renames); else nothing.
    std::optional<std::string> draw(const char* label, const std::string& current, float width = 0.0f);

private:
    ImGuiID editedId_ = 0;
    std::string text_;
};

// A small "?" with a tooltip, for hints that would clutter the layout.
void helpMarker(const char* text);

}  // namespace dmxviz::ui::fixture_editor
