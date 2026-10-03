#pragma once
// The default window arrangement, built with ImGui's DockBuilder:
//
//   +----------+------------------------+-----------+
//   | Outliner |        Viewport        | Inspector |
//   |          +------------------------+           |
//   |          | DMX Monitor | Log      |           |
//   +----------+------------------------+-----------+

namespace dmxviz::ui {

// Call once per frame right after ImGui::DockSpaceOverViewport() and before the
// panels are shown. Builds the default layout when the dockspace is still empty
// (first run without a layout .ini) or when `force` is set (View > Reset Layout).
void ensureDefaultDockLayout(unsigned int dockspaceId, bool force);

}  // namespace dmxviz::ui
