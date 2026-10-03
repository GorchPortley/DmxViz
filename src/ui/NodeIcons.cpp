#include "ui/NodeIcons.h"

#include <algorithm>
#include <initializer_list>

namespace dmxviz::ui {

namespace {

using stage::NodeKind;

// Maps a point of the unit square to screen space.
struct IconBox {
    ImDrawList* list;
    ImVec2 min;
    float size;
    ImU32 color;
    float thickness;

    ImVec2 at(float x, float y) const { return ImVec2(min.x + x * size, min.y + y * size); }
    void line(float x1, float y1, float x2, float y2) const { list->AddLine(at(x1, y1), at(x2, y2), color, thickness); }
    void rect(float x1, float y1, float x2, float y2) const {
        list->AddRect(at(x1, y1), at(x2, y2), color, 0.0f, 0, thickness);
    }
    void polyline(std::initializer_list<ImVec2> points, bool closed) const {
        for (const ImVec2& p : points) list->PathLineTo(at(p.x, p.y));
        list->PathStroke(color, closed ? ImDrawFlags_Closed : ImDrawFlags_None, thickness);
    }
};

}  // namespace

ImU32 nodeKindColor(NodeKind kind) {
    switch (kind) {
        case NodeKind::Group:
            return IM_COL32(230, 190, 90, 255);
        case NodeKind::Primitive:
            return IM_COL32(130, 190, 235, 255);
        case NodeKind::Model:
            return IM_COL32(170, 150, 235, 255);
        case NodeKind::Truss:
            return IM_COL32(190, 200, 210, 255);
        case NodeKind::StageDeck:
            return IM_COL32(150, 215, 160, 255);
        case NodeKind::Steps:
            return IM_COL32(150, 215, 160, 255);
        case NodeKind::Wall:
            return IM_COL32(215, 170, 130, 255);
        case NodeKind::Fixture:
            return IM_COL32(255, 215, 100, 255);
        case NodeKind::CameraPreset:
            return IM_COL32(120, 220, 215, 255);
        case NodeKind::ReferenceFigure:
            return IM_COL32(235, 140, 170, 255);
        case NodeKind::Unknown:
            break;
    }
    return IM_COL32(170, 170, 170, 255);
}

void drawNodeKindIcon(ImDrawList* list, ImVec2 min, float size, NodeKind kind, ImU32 color) {
    const IconBox b{list, min, size, color, std::max(1.0f, size * 0.09f)};
    switch (kind) {
        case NodeKind::Group:  // folder
            b.rect(0.08f, 0.30f, 0.92f, 0.82f);
            b.polyline({{0.08f, 0.30f}, {0.08f, 0.18f}, {0.40f, 0.18f}, {0.50f, 0.30f}}, false);
            break;
        case NodeKind::Primitive:  // cube
            b.polyline({{0.5f, 0.08f}, {0.9f, 0.3f}, {0.9f, 0.7f}, {0.5f, 0.92f}, {0.1f, 0.7f}, {0.1f, 0.3f}}, true);
            b.polyline({{0.1f, 0.3f}, {0.5f, 0.52f}, {0.9f, 0.3f}}, false);
            b.line(0.5f, 0.52f, 0.5f, 0.92f);
            break;
        case NodeKind::Model:  // triangle mesh
            b.polyline({{0.5f, 0.1f}, {0.92f, 0.85f}, {0.08f, 0.85f}}, true);
            b.line(0.5f, 0.1f, 0.5f, 0.85f);
            b.line(0.29f, 0.48f, 0.71f, 0.48f);
            break;
        case NodeKind::Truss:  // zig-zag between two chords
            b.rect(0.04f, 0.30f, 0.96f, 0.70f);
            b.polyline({{0.04f, 0.70f}, {0.22f, 0.30f}, {0.40f, 0.70f}, {0.58f, 0.30f}, {0.76f, 0.70f}, {0.94f, 0.30f}},
                       false);
            break;
        case NodeKind::StageDeck:  // platform on legs
            b.polyline({{0.22f, 0.32f}, {0.95f, 0.32f}, {0.78f, 0.58f}, {0.05f, 0.58f}}, true);
            b.line(0.10f, 0.58f, 0.10f, 0.86f);
            b.line(0.74f, 0.58f, 0.74f, 0.86f);
            break;
        case NodeKind::Steps:
            b.polyline({{0.05f, 0.85f},
                        {0.05f, 0.65f},
                        {0.35f, 0.65f},
                        {0.35f, 0.45f},
                        {0.65f, 0.45f},
                        {0.65f, 0.25f},
                        {0.95f, 0.25f},
                        {0.95f, 0.85f}},
                       true);
            break;
        case NodeKind::Wall:  // brick pattern
            b.rect(0.05f, 0.2f, 0.95f, 0.8f);
            b.line(0.05f, 0.5f, 0.95f, 0.5f);
            b.line(0.5f, 0.2f, 0.5f, 0.5f);
            b.line(0.3f, 0.5f, 0.3f, 0.8f);
            b.line(0.7f, 0.5f, 0.7f, 0.8f);
            break;
        case NodeKind::Fixture:  // lamp head with a beam
            b.rect(0.3f, 0.06f, 0.7f, 0.34f);
            b.polyline({{0.34f, 0.34f}, {0.66f, 0.34f}, {0.92f, 0.92f}, {0.08f, 0.92f}}, true);
            break;
        case NodeKind::CameraPreset:
            b.rect(0.05f, 0.3f, 0.65f, 0.75f);
            b.polyline({{0.65f, 0.52f}, {0.95f, 0.32f}, {0.95f, 0.73f}}, true);
            break;
        case NodeKind::ReferenceFigure:
            list->AddCircle(b.at(0.5f, 0.16f), size * 0.11f, color, 12, b.thickness);
            b.line(0.5f, 0.28f, 0.5f, 0.6f);
            b.line(0.24f, 0.42f, 0.76f, 0.42f);
            b.line(0.5f, 0.6f, 0.3f, 0.92f);
            b.line(0.5f, 0.6f, 0.7f, 0.92f);
            break;
        case NodeKind::Unknown:
            list->AddCircle(b.at(0.5f, 0.5f), size * 0.42f, color, 16, b.thickness);
            list->AddText(b.at(0.36f, 0.2f), color, "?");
            break;
    }
}

void drawEyeIcon(ImDrawList* list, ImVec2 min, float size, bool open, ImU32 color) {
    const IconBox b{list, min, size, color, std::max(1.0f, size * 0.09f)};
    list->PathLineTo(b.at(0.04f, 0.5f));
    list->PathBezierQuadraticCurveTo(b.at(0.5f, 0.0f), b.at(0.96f, 0.5f));
    list->PathBezierQuadraticCurveTo(b.at(0.5f, 1.0f), b.at(0.04f, 0.5f));
    list->PathStroke(color, ImDrawFlags_Closed, b.thickness);
    if (open) {
        list->AddCircleFilled(b.at(0.5f, 0.5f), size * 0.15f, color, 12);
    } else {
        b.line(0.12f, 0.9f, 0.88f, 0.1f);  // struck through
    }
}

void drawLockIcon(ImDrawList* list, ImVec2 min, float size, bool locked, ImU32 color) {
    const IconBox b{list, min, size, color, std::max(1.0f, size * 0.09f)};
    const ImVec2 bodyMin = b.at(0.2f, 0.46f);
    const ImVec2 bodyMax = b.at(0.8f, 0.92f);
    if (locked)
        list->AddRectFilled(bodyMin, bodyMax, color, size * 0.08f);
    else
        list->AddRect(bodyMin, bodyMax, color, size * 0.08f, 0, b.thickness);
    // Shackle: a half circle on top; when unlocked it is lifted on one side.
    const float lift = locked ? 0.0f : 0.14f;
    list->PathArcTo(b.at(0.5f, 0.42f - lift), size * 0.22f, kPi, 2.0f * kPi, 10);
    list->PathLineTo(b.at(0.72f, locked ? 0.46f : 0.38f));
    list->PathStroke(color, ImDrawFlags_None, b.thickness);
    list->AddLine(b.at(0.28f, 0.42f - lift), b.at(0.28f, 0.46f), color, b.thickness);
}

}  // namespace dmxviz::ui
