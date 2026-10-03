#pragma once
// Window titles of the panels that appear in the default dock layout. A panel's
// title() must return the same string for the layout to find it.

namespace dmxviz::ui {

inline constexpr const char* kViewportTitle = "Viewport";
inline constexpr const char* kOutlinerTitle = "Outliner";
inline constexpr const char* kInspectorTitle = "Inspector";
inline constexpr const char* kEnvironmentTitle = "Environment";
inline constexpr const char* kDmxMonitorTitle = "DMX Monitor";
inline constexpr const char* kLogTitle = "Log";
inline constexpr const char* kFixtureLibraryTitle = "Fixture Library";
inline constexpr const char* kPatchTitle = "Patch";
inline constexpr const char* kDmxInterfacesTitle = "DMX Interfaces";
inline constexpr const char* kTestConsoleTitle = "Test Console";

}  // namespace dmxviz::ui
