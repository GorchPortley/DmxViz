#include "ui/DockLayout.h"

#include "ui/PanelTitles.h"

#include "imgui.h"
#include "imgui_internal.h"  // DockBuilder is not part of the public API yet

namespace dmxviz::ui {

void ensureDefaultDockLayout(unsigned int dockspaceId, bool force) {
    const ImGuiDockNode* existing = ImGui::DockBuilderGetNode(dockspaceId);
    // A layout restored from the .ini has child nodes; a fresh dockspace is an empty leaf.
    const bool isEmpty = existing == nullptr || (existing->IsEmpty() && !existing->IsSplitNode());
    if (!force && !isEmpty) return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);
    ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);

    // Split off the side columns first, then the bottom strip from what is left.
    ImGuiID rest = dockspaceId;
    ImGuiID left = 0;
    ImGuiID right = 0;
    ImGuiID bottom = 0;
    left = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Left, 0.17f, nullptr, &rest);
    right = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, 0.22f, nullptr, &rest);
    bottom = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Down, 0.27f, nullptr, &rest);

    ImGui::DockBuilderDockWindow(kOutlinerTitle, left);
    ImGui::DockBuilderDockWindow(kFixtureLibraryTitle, left);
    ImGui::DockBuilderDockWindow(kInspectorTitle, right);
    ImGui::DockBuilderDockWindow(kDmxMonitorTitle, bottom);
    ImGui::DockBuilderDockWindow(kPatchTitle, bottom);
    ImGui::DockBuilderDockWindow(kLogTitle, bottom);
    ImGui::DockBuilderDockWindow(kViewportTitle, rest);
    ImGui::DockBuilderFinish(dockspaceId);
}

}  // namespace dmxviz::ui
