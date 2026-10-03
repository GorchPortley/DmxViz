#include "ui/ViewportPanel.h"

#include "ui/EditorContext.h"
#include "ui/PanelTitles.h"

#include "stage/Commands.h"
#include "stage/PlacementTools.h"

#include "imgui.h"
#include "ImGuizmo.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>

namespace dmxviz::ui {

namespace {

constexpr float kClickMaxMovePixels = 4.0f;
constexpr float kToolbarMargin = 8.0f;
const glm::vec3 kDuplicateOffset{0.5f, 0.0f, 0.0f};  // so a copy is visible next to its original

bool anyCameraButtonDown(const ImGuiIO& io) {
    return ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
           (io.KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left));
}

const char* toolName(ViewportPanel::GizmoTool tool) {
    switch (tool) {
        case ViewportPanel::GizmoTool::Translate: return "Move";
        case ViewportPanel::GizmoTool::Rotate: return "Rotate";
        case ViewportPanel::GizmoTool::Scale: return "Scale";
    }
    return "Transform";
}

// A toolbar button that shows whether it is the active choice.
bool toggleButton(const char* label, bool active, const char* tooltip) {
    if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    const bool pressed = ImGui::Button(label);
    if (active) ImGui::PopStyleColor();
    if (tooltip != nullptr) ImGui::SetItemTooltip("%s", tooltip);
    return pressed;
}

}  // namespace

ViewportPanel::ViewportPanel() = default;

const char* ViewportPanel::title() const { return kViewportTitle; }

int ViewportPanel::windowFlags() const { return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse; }

// ---------------------------------------------------------------------------
// Frame

void ViewportPanel::draw(EditorContext& ctx) {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 8.0f || avail.y < 8.0f) return;

    // 1. Render the scene at the panel's pixel size and show it.
    const int pixelsW = std::max(1, static_cast<int>(avail.x * ctx.dpiScale));
    const int pixelsH = std::max(1, static_cast<int>(avail.y * ctx.dpiScale));
    target_.resize(pixelsW, pixelsH);
    const float aspect = static_cast<float>(pixelsW) / static_cast<float>(pixelsH);
    const render::Camera cam = camera_.camera(aspect);
    ctx.renderer.render(target_, cam, ctx.frame, ctx.assets);
    ImGui::Image(ImTextureRef(target_.imguiTexture()), avail);
    const ViewRect rect{ImGui::GetItemRectMin(), avail};

    // 2. Overlays (their widgets decide below whether the mouse is "in the view").
    drawToolbar(ctx, rect);
    drawStats(ctx, rect);

    const bool hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(rect.min, ImVec2(rect.min.x + rect.size.x, rect.min.y + rect.size.y)) &&
                         !ImGui::IsAnyItemHovered();

    // 3. Camera first: while it is being dragged, gizmo and selection stay out of the way.
    const bool cameraUsed = handleViewportCameraInput(camera_, hovered, rect.size.y, io.DeltaTime);
    const bool cameraBusy = cameraUsed && anyCameraButtonDown(io);

    handleGizmo(ctx, rect, cam, cameraBusy);
    handleSelection(ctx, rect, aspect, hovered, cameraBusy);
    handleKeyboard(ctx, hovered || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));
}

// ---------------------------------------------------------------------------
// Overlays

void ViewportPanel::drawToolbar(EditorContext& ctx, const ViewRect& rect) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.10f, 0.12f, 0.72f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.10f, 0.12f, 0.72f));

    // Row 1: transform tools.
    ImGui::SetCursorScreenPos(ImVec2(rect.min.x + kToolbarMargin, rect.min.y + kToolbarMargin));
    if (toggleButton("Move", tool_ == GizmoTool::Translate, "Move (W)")) tool_ = GizmoTool::Translate;
    ImGui::SameLine();
    if (toggleButton("Rotate", tool_ == GizmoTool::Rotate, "Rotate (E)")) tool_ = GizmoTool::Rotate;
    ImGui::SameLine();
    if (toggleButton("Scale", tool_ == GizmoTool::Scale, "Scale (R)")) tool_ = GizmoTool::Scale;
    ImGui::SameLine();
    if (ImGui::Button(localSpace_ ? "Local" : "World")) localSpace_ = !localSpace_;
    ImGui::SetItemTooltip("Gizmo space: local or world axes (scale is always local)");
    ImGui::SameLine();
    if (toggleButton("Snap", snapEnabled_, "Snap moves to the grid and rotations to angle steps")) snapEnabled_ = !snapEnabled_;
    if (snapEnabled_) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(64.0f);
        ImGui::DragFloat("##gridStep", &gridStep_, 0.01f, 0.01f, 10.0f, "%.2f m");
        ImGui::SetItemTooltip("Grid step for moving");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(56.0f);
        ImGui::DragFloat("##angleStep", &angleStep_, 0.5f, 1.0f, 90.0f, "%.0f deg");
        ImGui::SetItemTooltip("Angle step for rotating");
    }

    // Row 2: camera presets and framing.
    ImGui::SetCursorScreenPos(ImVec2(rect.min.x + kToolbarMargin, ImGui::GetCursorScreenPos().y));
    struct PresetButton {
        const char* label;
        ViewportCamera::Preset preset;
    };
    static constexpr PresetButton kPresets[] = {{"Front", ViewportCamera::Preset::Front},
                                                {"Side", ViewportCamera::Preset::Side},
                                                {"Top", ViewportCamera::Preset::Top},
                                                {"Perspective", ViewportCamera::Preset::Perspective},
                                                {"Audience", ViewportCamera::Preset::Audience}};
    for (const PresetButton& p : kPresets) {
        if (&p != &kPresets[0]) ImGui::SameLine();
        if (ImGui::Button(p.label)) showPreset(ctx, p.preset);
    }
    ImGui::SameLine();
    if (ImGui::Button("Frame")) frameSelection(ctx);
    ImGui::SetItemTooltip("Frame the selection, or the whole stage (F)");

    ImGui::PopStyleColor(2);
}

void ViewportPanel::drawStats(EditorContext& ctx, const ViewRect& rect) const {
    const render::RenderStats& s = ctx.renderer.stats();
    char text[160];
    std::snprintf(text, sizeof text, "%.0f fps   %d meshes   %d beams   %d draw calls", ImGui::GetIO().Framerate,
                  s.meshInstances, s.beams, s.drawCalls);
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 at(rect.min.x + kToolbarMargin, rect.min.y + rect.size.y - ImGui::GetTextLineHeight() - kToolbarMargin);
    list->AddText(ImVec2(at.x + 1.0f, at.y + 1.0f), IM_COL32(0, 0, 0, 200), text);
    list->AddText(at, IM_COL32(200, 200, 205, 220), text);
}

// ---------------------------------------------------------------------------
// Gizmo

std::vector<NodeId> ViewportPanel::editableSelection(const EditorContext& ctx) const {
    std::vector<NodeId> result;
    if (ctx.selection.empty()) return result;
    for (NodeId id : ctx.scene.topLevelOnly(ctx.selection.ids()))
        if (!ctx.scene.effectiveLocked(id)) result.push_back(id);
    return result;
}

void ViewportPanel::handleGizmo(EditorContext& ctx, const ViewRect& rect, const render::Camera& cam, bool cameraBusy) {
    const std::vector<NodeId> targets = editableSelection(ctx);
    if (targets.empty()) {
        if (dragging_) endGizmoDrag(ctx);
        return;
    }

    // The gizmo sits on the primary node (or the last editable one).
    const auto primary = std::find(targets.begin(), targets.end(), ctx.selection.primary());
    const NodeId pivotId = primary != targets.end() ? *primary : targets.back();
    glm::mat4 pivot = ctx.scene.worldMatrix(pivotId);

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(rect.min.x, rect.min.y, rect.size.x, rect.size.y);
    ImGuizmo::Enable(!cameraBusy);

    ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
    float snapValues[3] = {gridStep_, gridStep_, gridStep_};
    if (tool_ == GizmoTool::Rotate) {
        operation = ImGuizmo::ROTATE;
        snapValues[0] = angleStep_;
    } else if (tool_ == GizmoTool::Scale) {
        operation = ImGuizmo::SCALE;
        snapValues[0] = 0.1f;
    }
    const ImGuizmo::MODE mode = (localSpace_ || tool_ == GizmoTool::Scale) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

    glm::mat4 manipulated = pivot;
    const bool changed = ImGuizmo::Manipulate(&cam.view[0][0], &cam.projection[0][0], operation, mode,
                                              &manipulated[0][0], nullptr, snapEnabled_ ? snapValues : nullptr);
    const bool gizmoActive = ImGuizmo::IsUsing();
    if (gizmoActive && !dragging_) beginGizmoDrag(ctx, targets, pivot);
    if (dragging_ && changed) applyGizmoDrag(ctx, manipulated);
    if (!gizmoActive && dragging_) endGizmoDrag(ctx);
}

void ViewportPanel::beginGizmoDrag(EditorContext& ctx, const std::vector<NodeId>& targets, const glm::mat4& pivot) {
    dragging_ = true;
    dragStartPivot_ = pivot;
    dragStartWorlds_.clear();
    for (NodeId id : targets) dragStartWorlds_.emplace_back(id, ctx.scene.worldMatrix(id));
    ctx.commands.breakMerge();  // this drag must not merge into an earlier edit
}

void ViewportPanel::applyGizmoDrag(EditorContext& ctx, const glm::mat4& newPivot) {
    // Every selected node follows the pivot's total movement since the drag began.
    const glm::mat4 delta = newPivot * glm::inverse(dragStartPivot_);
    std::vector<std::pair<NodeId, stage::Transform>> updates;
    updates.reserve(dragStartWorlds_.size());
    for (const auto& [id, startWorld] : dragStartWorlds_) {
        const stage::Node* node = ctx.scene.find(id);
        if (node == nullptr) continue;
        updates.emplace_back(id, ctx.scene.localFromWorld(node->parent(), delta * startWorld));
    }
    if (updates.empty()) return;
    // The steps of one drag merge into a single undo entry (SetTransformCommand::mergeWith).
    ctx.commands.execute(std::make_unique<stage::SetTransformCommand>(std::move(updates), toolName(tool_)));
}

void ViewportPanel::endGizmoDrag(EditorContext& ctx) {
    dragging_ = false;
    dragStartWorlds_.clear();
    ctx.commands.breakMerge();
}

// ---------------------------------------------------------------------------
// Selection and hover

NodeId ViewportPanel::pickAt(EditorContext& ctx, const ViewRect& rect, float aspect, const ImVec2& mouse) {
    const glm::vec2 ndc((mouse.x - rect.min.x) / rect.size.x * 2.0f - 1.0f,
                        1.0f - (mouse.y - rect.min.y) / rect.size.y * 2.0f);
    const Ray ray = camera_.rayThrough(ndc, aspect);
    const auto skipLocked = [&ctx](const MeshInstance& instance) { return !ctx.scene.effectiveLocked(instance.pickId); };
    const std::optional<stage::PickHit> hit = picker_.pick(ray, ctx.frame.meshes, ctx.assets, skipLocked);
    return hit ? hit->node : kInvalidNode;
}

void ViewportPanel::handleSelection(EditorContext& ctx, const ViewRect& rect, float aspect, bool hovered,
                                    bool cameraBusy) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool gizmoBusy = ImGuizmo::IsOver() || ImGuizmo::IsUsing() || dragging_;

    if (hovered && !gizmoBusy && !io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        pressActive_ = true;
        pressPos_ = io.MousePos;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseReleased(ImGuiMouseButton_Left)) pressActive_ = false;

    if (pressActive_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        pressActive_ = false;
        const float dx = io.MousePos.x - pressPos_.x;
        const float dy = io.MousePos.y - pressPos_.y;
        const bool isClick = dx * dx + dy * dy <= kClickMaxMovePixels * kClickMaxMovePixels;
        if (isClick && !cameraBusy && !ImGuizmo::IsUsing()) {
            const NodeId hit = pickAt(ctx, rect, aspect, io.MousePos);
            if (hit != kInvalidNode) {
                if (io.KeyCtrl)
                    ctx.selection.toggle(hit);
                else if (io.KeyShift)
                    ctx.selection.add(hit);
                else
                    ctx.selection.set(hit);
            } else if (!io.KeyCtrl && !io.KeyShift) {
                ctx.selection.clear();
            }
        }
    }

    updateHover(ctx, rect, aspect, hovered, gizmoBusy || cameraBusy || pressActive_);
}

void ViewportPanel::updateHover(EditorContext& ctx, const ViewRect& rect, float aspect, bool hovered, bool busy) {
    const ImGuiIO& io = ImGui::GetIO();
    if (!hovered || busy) {
        if (hoverOwned_) {
            ctx.selection.setHover(kInvalidNode);
            hoverOwned_ = false;
        }
        return;
    }
    // Picking is a ray cast per instance, so only redo it when the mouse moved.
    if (io.MouseDelta.x == 0.0f && io.MouseDelta.y == 0.0f && hoverOwned_) return;
    ctx.selection.setHover(pickAt(ctx, rect, aspect, io.MousePos));
    hoverOwned_ = true;
}

// ---------------------------------------------------------------------------
// Keyboard

void ViewportPanel::handleKeyboard(EditorContext& ctx, bool active) {
    const ImGuiIO& io = ImGui::GetIO();
    if (!active || io.WantTextInput) return;

    // While the right button is held, W/E/Q/A/S/D fly the camera instead.
    const bool flying = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (!flying && !io.KeyCtrl && !io.KeyAlt) {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) tool_ = GizmoTool::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) tool_ = GizmoTool::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) tool_ = GizmoTool::Scale;
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) frameSelection(ctx);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) deleteSelection(ctx);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicateSelection(ctx);
}

void ViewportPanel::deleteSelection(EditorContext& ctx) {
    const std::vector<NodeId> ids = editableSelection(ctx);
    if (ids.empty()) return;
    ctx.commands.execute(std::make_unique<stage::DeleteNodesCommand>(ids));
    ctx.selection.setHover(kInvalidNode);
    ctx.selection.prune(ctx.scene);
}

void ViewportPanel::duplicateSelection(EditorContext& ctx) {
    const std::vector<NodeId> ids = editableSelection(ctx);
    if (ids.empty()) return;
    stage::Command* done = ctx.commands.execute(stage::makeDuplicateCommand(ctx.scene, ids, kDuplicateOffset));
    if (done != nullptr) {
        const std::vector<NodeId> copies = done->resultNodes();
        if (!copies.empty()) ctx.selection.setMany(copies);
    }
}

// ---------------------------------------------------------------------------
// Camera helpers

Aabb ViewportPanel::stageBounds(EditorContext& ctx) const {
    Aabb bounds;
    Aabb withPlanes;
    for (NodeId root : ctx.scene.roots()) {
        const Aabb b = ctx.scene.worldBounds(root, ctx.assets);
        withPlanes.expand(b);
        // A big floor plane would make the "whole stage" view far too wide.
        const stage::Node* node = ctx.scene.find(root);
        const stage::PrimitiveContent* prim = node ? node->as<stage::PrimitiveContent>() : nullptr;
        if (prim != nullptr && prim->shape == stage::PrimitiveShape::Plane) continue;
        bounds.expand(b);
    }
    if (bounds.empty()) bounds = withPlanes;
    if (bounds.empty()) {
        bounds.min = glm::vec3(-5.0f, 0.0f, -5.0f);
        bounds.max = glm::vec3(5.0f, 4.0f, 5.0f);
    }
    return bounds;
}

void ViewportPanel::frameSelection(EditorContext& ctx) {
    if (!ctx.selection.empty()) {
        const Aabb bounds = stage::tools::selectionBounds(ctx.scene, ctx.assets, ctx.selection.ids());
        if (!bounds.empty()) {
            camera_.frame(bounds);
            return;
        }
    }
    camera_.frame(stageBounds(ctx));
}

void ViewportPanel::showPreset(EditorContext& ctx, ViewportCamera::Preset preset) {
    camera_.setPreset(preset, stageBounds(ctx));
}

}  // namespace dmxviz::ui
