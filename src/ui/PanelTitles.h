#pragma once
// Window titles of the panels that appear in the default dock layout. A panel's
// title() must return the same string for the layout to find it.

namespace dmxviz::ui {

inline constexpr const char* kViewportTitle = "Viewport";
inline constexpr const char* kOutlinerTitle = "Outliner";
inline constexpr const char* kInspectorTitle = "Inspector";
inline constexpr const char* kDmxMonitorTitle = "DMX Monitor";
inline constexpr const char* kLogTitle = "Log";

}  // namespace dmxviz::ui
