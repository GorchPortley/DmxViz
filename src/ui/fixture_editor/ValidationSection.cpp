#include "ui/fixture_editor/ValidationSection.h"

#include "ui/fixture_editor/EditorWidgets.h"

#include "imgui.h"

namespace dmxviz::ui::fixture_editor {

const char* ValidationSection::areaName(ProblemArea area) {
    switch (area) {
        case ProblemArea::General:
            return "General";
        case ProblemArea::Geometry:
            return "Geometry";
        case ProblemArea::Wheels:
            return "Wheels";
        case ProblemArea::Modes:
            return "Modes & Channels";
    }
    return "";
}

std::optional<Problem> ValidationSection::draw(const std::vector<Problem>& problems) {
    const ProblemCounts counts = countProblems(problems);
    if (problems.empty()) {
        ImGui::TextColored(okColor(), "No problems found.");
        return std::nullopt;
    }
    ImGui::TextColored(counts.errors > 0 ? errorColor() : okColor(), "%d error%s", counts.errors,
                       counts.errors == 1 ? "" : "s");
    ImGui::SameLine();
    ImGui::TextColored(counts.warnings > 0 ? warningColor() : okColor(), "%d warning%s", counts.warnings,
                       counts.warnings == 1 ? "" : "s");
    ImGui::SameLine();
    ImGui::TextDisabled("(errors block Apply; click a line to jump there)");

    std::optional<Problem> clicked;
    bool firstInArea = true;
    ProblemArea area = ProblemArea::General;
    ImGui::BeginChild("##problemList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    for (std::size_t i = 0; i < problems.size(); ++i) {
        const Problem& p = problems[i];
        if (firstInArea || p.area != area) {
            ImGui::SeparatorText(areaName(p.area));
            area = p.area;
            firstInArea = false;
        }
        ImGui::PushID(static_cast<int>(i));
        const bool error = p.severity == Severity::Error;
        ImGui::PushStyleColor(ImGuiCol_Text, error ? errorColor() : warningColor());
        ImGui::TextUnformatted(error ? "Error  " : "Warning");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextDisabled("%s", p.where.c_str());
        ImGui::TextWrapped("%s", p.message.c_str());
        ImGui::EndGroup();
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemClicked()) clicked = p;
        ImGui::PopID();
    }
    ImGui::EndChild();
    return clicked;
}

}  // namespace dmxviz::ui::fixture_editor
