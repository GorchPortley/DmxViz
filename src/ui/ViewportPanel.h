#pragma once
// ViewportPanel: the 3D view of the stage.
//
// It renders the frame's RenderScene into its own offscreen target and shows it
// as an image, then handles everything you do *in* the view:
//
//   camera ........ see ViewportCamera.h (middle drag orbits, wheel dollies, right button + WASDQE flies)
//   select ........ left click picks, Ctrl/Shift+click toggles/adds, click on empty space clears,
//                   moving the mouse over an object highlights it
//   transform ..... ImGuizmo gizmo on the selection; W / E / R = move / rotate / scale,
//                   toolbar: local/world, grid snap (0.25 m, 15 deg); a drag is one undo step
//   F ............. frame the selection (or the whole stage)
//   drag & drop ... drop a fixture type from the Fixture Library: onto a truss it hangs there,
//                   anywhere else it is placed on the surface under the pointer (FixtureSpawner)
//   Delete ........ delete the selection, Ctrl+D duplicates it
// Undo/redo (Ctrl+Z / Ctrl+Y) are global shortcuts handled by the application.
//
// Gizmo and clicks are ignored while the camera is being dragged.

#include "core/Math.h"
#include "render/Renderer.h"
#include "stage/Picking.h"
#include "ui/Panel.h"
#include "ui/ViewportCamera.h"

#include "imgui.h"

#include <optional>
#include <utility>
#include <vector>

namespace dmxviz::ui {

class ViewportPanel : public Panel {
public:
    enum class GizmoTool { Translate, Rotate, Scale };

    ViewportPanel();

    const char* title() const override;
    void draw(EditorContext& ctx) override;
    int windowFlags() const override;
    bool windowPadding() const override { return false; }

    ViewportCamera& camera() { return camera_; }
    // Frames the selection, or the whole stage when nothing is selected.
    void frameSelection(EditorContext& ctx);
    // Moves the camera to one of the standard views of the whole stage.
    void showPreset(EditorContext& ctx, ViewportCamera::Preset preset);

private:
    // Screen rectangle of the rendered image, in ImGui coordinates.
    struct ViewRect {
        ImVec2 min;
        ImVec2 size;
    };

    void drawToolbar(EditorContext& ctx, const ViewRect& rect);
    void drawStats(EditorContext& ctx, const ViewRect& rect) const;
    void handleKeyboard(EditorContext& ctx, bool active);
    void handleGizmo(EditorContext& ctx, const ViewRect& rect, const render::Camera& cam, bool cameraBusy);
    void handleSelection(EditorContext& ctx, const ViewRect& rect, float aspect, bool hovered, bool cameraBusy);
    void updateHover(EditorContext& ctx, const ViewRect& rect, float aspect, bool hovered, bool busy);

    void beginGizmoDrag(EditorContext& ctx, const std::vector<NodeId>& targets, const glm::mat4& pivot);
    void applyGizmoDrag(EditorContext& ctx, const glm::mat4& newPivot);
    void endGizmoDrag(EditorContext& ctx);

    // Node under a screen position (kInvalidNode if none).
    NodeId pickAt(EditorContext& ctx, const ViewRect& rect, float aspect, const ImVec2& mouse);
    // Same ray cast, with the hit point and surface normal. With `floorFallback` a ray that
    // hits nothing lands on the y = 0 plane (a hit without node).
    std::optional<stage::PickHit> pickHitAt(EditorContext& ctx, const ViewRect& rect, float aspect,
                                            const ImVec2& mouse, bool floorFallback = false);
    // Drop target for fixture types dragged from the Fixture Library. Must be called right
    // after the viewport image is submitted (that image is the drop target's item).
    void handleFixtureDrop(EditorContext& ctx, const ViewRect& rect, float aspect);
    // Top-level, unlocked selected nodes: the ones the gizmo moves.
    std::vector<NodeId> editableSelection(const EditorContext& ctx) const;
    Aabb stageBounds(EditorContext& ctx) const;
    void deleteSelection(EditorContext& ctx);
    void duplicateSelection(EditorContext& ctx);

    render::ViewportTarget target_;
    ViewportCamera camera_;
    stage::Picker picker_;

    // Gizmo settings (toolbar).
    GizmoTool tool_ = GizmoTool::Translate;
    bool localSpace_ = false;
    bool snapEnabled_ = true;
    float gridStep_ = 0.25f;   // metres
    float angleStep_ = 15.0f;  // degrees

    bool gizmoShown_ = false;  // the gizmo was updated this frame

    // Gizmo drag in progress: world matrices at the start, so the result never drifts.
    bool dragging_ = false;
    glm::mat4 dragStartPivot_{1.0f};
    std::vector<std::pair<NodeId, glm::mat4>> dragStartWorlds_;

    // Click-to-select: a click counts when the mouse hardly moved between press and release.
    bool pressActive_ = false;
    ImVec2 pressPos_{0.0f, 0.0f};
    bool hoverOwned_ = false;  // this panel set the selection's hover node
};

}  // namespace dmxviz::ui
