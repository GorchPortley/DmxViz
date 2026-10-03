#include "ui/fixture_editor/EditorWidgets.h"

#include "imgui_stdlib.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace dmxviz::ui::fixture_editor {

namespace {

bool containsNoCase(std::string_view text, std::string_view needle) {
    if (needle.empty()) return true;
    auto it = std::search(text.begin(), text.end(), needle.begin(), needle.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return it != text.end();
}

}  // namespace

ImVec4 errorColor() {
    return ImVec4(1.0f, 0.45f, 0.40f, 1.0f);
}

ImVec4 warningColor() {
    return ImVec4(1.0f, 0.78f, 0.35f, 1.0f);
}

ImVec4 okColor() {
    return ImVec4(0.55f, 0.85f, 0.55f, 1.0f);
}

bool inputText(const char* label, std::string& text, ImGuiInputTextFlags flags) {
    return ImGui::InputText(label, &text, flags);
}

bool dragDegrees(const char* label, float& radians, float speed, float minDegrees, float maxDegrees, const char* format) {
    float degrees = radToDeg(radians);
    if (!ImGui::DragFloat(label, &degrees, speed, minDegrees, maxDegrees, format)) return false;
    radians = degToRad(degrees);
    return true;
}

bool dragVec3(const char* label, glm::vec3& value, float speed, float min, float max, const char* format) {
    return ImGui::DragFloat3(label, &value.x, speed, min, max, format);
}

bool comboIndex(const char* label, int& index, std::span<const char* const> names) {
    bool changed = false;
    const int count = static_cast<int>(names.size());
    const char* preview = index >= 0 && index < count ? names[static_cast<std::size_t>(index)] : "";
    if (ImGui::BeginCombo(label, preview)) {
        for (int i = 0; i < count; ++i) {
            if (ImGui::Selectable(names[static_cast<std::size_t>(i)], i == index)) {
                index = i;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool AttributePicker::draw(const char* label, fixtures::Attribute& value, float width) {
    if (width != 0.0f) ImGui::SetNextItemWidth(width);  // negative = fill the cell
    const std::string_view preview = fixtures::attributeInfo(value).name;
    bool changed = false;
    if (ImGui::BeginCombo(label, std::string(preview).c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsWindowAppearing()) {
            filter_.clear();
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##attributeFilter", "Search attributes", &filter_);
        ImGui::Separator();
        for (const fixtures::AttributeInfo& info : fixtures::allAttributes()) {
            if (!containsNoCase(info.name, filter_) && !containsNoCase(info.pretty, filter_) &&
                !containsNoCase(info.group, filter_))
                continue;
            ImGui::PushID(static_cast<int>(info.attribute));
            if (ImGui::Selectable(std::string(info.name).c_str(), info.attribute == value)) {
                value = info.attribute;
                changed = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", std::string(info.group).c_str());
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

void helpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

}  // namespace dmxviz::ui::fixture_editor
