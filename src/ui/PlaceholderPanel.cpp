#include "ui/PlaceholderPanel.h"

#include "imgui.h"

namespace dmxviz::ui {

void PlaceholderPanel::draw(EditorContext& ctx) {
    (void)ctx;
    ImGui::TextDisabled("%s: coming soon", title_.c_str());
}

}  // namespace dmxviz::ui
