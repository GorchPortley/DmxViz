#include "ui/EditSession.h"

#include "imgui.h"

namespace dmxviz::ui {

bool EditSession::touch(bool widgetChanged, const char* label) {
    // Pressing a widget starts a new gesture, so it must not merge into the previous undo step.
    if (ImGui::IsItemActivated()) ctx_.commands.breakMerge();
    if (widgetChanged) {
        changed_ = true;
        label_ = label;
        // Sliders keep merging while the mouse is held; clicks, combos and typing end the step at once.
        if (!ImGui::IsItemActive()) gestureEnded_ = true;
    }
    if (ImGui::IsItemDeactivated()) gestureEnded_ = true;
    return widgetChanged;
}

std::unique_ptr<stage::EditNodesCommand> EditSession::makeCommand(
    std::vector<std::pair<NodeId, stage::NodeData>> after) const {
    return std::make_unique<stage::EditNodesCommand>(std::move(after), "Set " + label_, label_);
}

void EditSession::finish() {
    if (gestureEnded_) ctx_.commands.breakMerge();
}

}  // namespace dmxviz::ui
