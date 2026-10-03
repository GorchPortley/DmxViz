#pragma once
// NodeIcons: tiny vector icons for the outliner (one per node kind, plus the
// visibility eye and the lock). They are drawn with the ImGui draw list so no
// icon font or image files are needed.

#include "stage/NodeContent.h"

#include "imgui.h"

namespace dmxviz::ui {

// Colour used for the icon and label accent of a node kind.
ImU32 nodeKindColor(stage::NodeKind kind);

// All icons are drawn inside the square [min, min + size].
void drawNodeKindIcon(ImDrawList* list, ImVec2 min, float size, stage::NodeKind kind, ImU32 color);
void drawEyeIcon(ImDrawList* list, ImVec2 min, float size, bool open, ImU32 color);
void drawLockIcon(ImDrawList* list, ImVec2 min, float size, bool locked, ImU32 color);

}  // namespace dmxviz::ui
