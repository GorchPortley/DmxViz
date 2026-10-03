#include "ui/Panel.h"

#include "ui/EditorContext.h"

#include "imgui.h"

namespace dmxviz::ui {

void Panel::show(EditorContext& ctx) {
    if (!open) return;
    if (!windowPadding()) ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin(title(), &open, windowFlags());
    if (!windowPadding()) ImGui::PopStyleVar();
    if (visible) draw(ctx);
    ImGui::End();
}

}  // namespace dmxviz::ui
