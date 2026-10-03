#include "ui/InspectorPanel.h"

#include "stage/Commands.h"
#include "stage/PathUtil.h"
#include "stage/PlacementTools.h"
#include "ui/ColorWidgets.h"
#include "ui/ContentEditors.h"
#include "ui/EditSession.h"
#include "ui/EditorContext.h"
#include "ui/NodeActions.h"
#include "ui/NodeIcons.h"
#include "ui/PanelTitles.h"
#include "ui/ViewportCamera.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>

namespace dmxviz::ui {

using namespace stage;

namespace {

// True for kinds that emit meshes, i.e. where a material override makes sense.
bool hasGeometry(NodeKind kind) {
    switch (kind) {
        case NodeKind::Primitive:
        case NodeKind::Model:
        case NodeKind::Truss:
        case NodeKind::StageDeck:
        case NodeKind::Steps:
        case NodeKind::Wall:
        case NodeKind::ReferenceFigure:
            return true;
        default:
            return false;
    }
}

std::string kindLabel(NodeKind kind) {
    return std::string(nodeKindLabel(kind));
}

// Kind icon followed by the kind name and the node id.
void drawKindLine(const Node& node) {
    const float size = ImGui::GetFontSize();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    drawNodeKindIcon(ImGui::GetWindowDrawList(), pos, size, node.kind(), nodeKindColor(node.kind()));
    ImGui::Dummy(ImVec2(size, size));
    ImGui::SameLine();
    ImGui::Text("%s", kindLabel(node.kind()).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("#%llu", static_cast<unsigned long long>(node.id()));
}

}  // namespace

const char* InspectorPanel::title() const {
    return kInspectorTitle;
}

void InspectorPanel::draw(EditorContext& ctx) {
    pollModelDialog(ctx);
    ImGui::PushItemWidth(-ImGui::GetFontSize() * 7.5f);

    const Node* primary = ctx.scene.find(ctx.selection.primary());
    if (ctx.selection.empty() || primary == nullptr) {
        ImGui::TextDisabled("Nothing selected.");
        ImGui::TextDisabled("Click an object in the viewport or the outliner.");
    } else if (ctx.selection.size() == 1) {
        drawSingle(ctx, *primary);
    } else {
        drawMultiple(ctx);
    }
    ImGui::PopItemWidth();
}

// ---------------------------------------------------------------------------
// One node

void InspectorPanel::drawSingle(EditorContext& ctx, const Node& node) {
    const NodeId id = node.id();
    NodeData data = node.data();  // the widgets edit this copy; a changed copy becomes one command
    EditSession edit(ctx);

    drawHeader(ctx, node, edit, data);
    drawTransform(ctx, node);
    drawContent(ctx, node, edit, data);

    if (edit.changed()) ctx.commands.execute(edit.makeCommand({{id, std::move(data)}}));
    edit.finish();
}

void InspectorPanel::drawHeader(EditorContext& ctx, const Node& node, EditSession& edit, NodeData& data) {
    drawKindLine(node);

    char name[256];
    std::snprintf(name, sizeof(name), "%s", data.name.c_str());
    if (edit.touch(ImGui::InputText("Name", name, sizeof(name)), "Name", "Rename")) data.name = name;

    edit.touch(ImGui::Checkbox("Visible", &data.visible), "Visible", data.visible ? "Show" : "Hide");
    ImGui::SameLine();
    edit.touch(ImGui::Checkbox("Locked", &data.locked), "Locked", data.locked ? "Lock" : "Unlock");

    const std::vector<Layer>& layers = ctx.scene.layers();
    if (layers.size() > 1) {
        const int layerIndex = std::clamp(data.layer, 0, static_cast<int>(layers.size()) - 1);
        bool changed = false;
        if (ImGui::BeginCombo("Layer", layers[static_cast<std::size_t>(layerIndex)].name.c_str())) {
            for (std::size_t i = 0; i < layers.size(); ++i) {
                if (ImGui::Selectable(layers[i].name.c_str(), static_cast<int>(i) == layerIndex)) {
                    data.layer = static_cast<int>(i);
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        edit.touch(changed, "Layer");
    }
}

// ---------------------------------------------------------------------------
// Transform

glm::vec3 InspectorPanel::eulerFor(const Node& node) {
    const glm::quat& q = node.transform().rotation;
    // Keep the angles the user typed while they still describe the same rotation.
    if (node.id() != eulerNode_ || std::abs(glm::dot(q, eulerQuat_)) < 0.99999f) {
        eulerNode_ = node.id();
        euler_ = node.transform().eulerDegrees() + glm::vec3(0.0f);  // + 0 turns -0.0 into 0.0
        eulerQuat_ = q;
    }
    return euler_;
}

void InspectorPanel::drawTransform(EditorContext& ctx, const Node& node) {
    ImGui::SeparatorText(node.parent() == kInvalidNode ? "Transform" : "Transform (relative to parent)");

    Transform t = node.transform();
    glm::vec3 euler = eulerFor(node);
    EditSession edit(ctx);

    edit.touch(ImGui::DragFloat3("Position (m)", &t.position.x, 0.01f, 0.0f, 0.0f, "%.3f"), "Position", "Move");
    if (edit.touch(ImGui::DragFloat3("Rotation (deg)", &euler.x, 0.25f, 0.0f, 0.0f, "%.1f"), "Rotation", "Rotate")) {
        t.rotation = Transform::quatFromEulerDegrees(euler);
        euler_ = euler;
        eulerQuat_ = t.rotation;
    }

    const glm::vec3 oldScale = t.scale;
    if (edit.touch(ImGui::DragFloat3("Scale", &t.scale.x, 0.01f, 0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp),
                   "Scale", "Scale") &&
        uniformScale_) {
        // Keep the proportions: the component that moved decides the factor for all three.
        for (int i = 0; i < 3; ++i) {
            if (t.scale[i] == oldScale[i]) continue;
            t.scale = oldScale[i] != 0.0f ? oldScale * (t.scale[i] / oldScale[i]) : glm::vec3(t.scale[i]);
            break;
        }
    }
    ImGui::Checkbox("Uniform scale", &uniformScale_);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset transform")) {
        t = Transform{};
        euler_ = glm::vec3(0.0f);
        eulerQuat_ = t.rotation;
        edit.touch(true, "Reset transform", "Reset transform");
    }

    if (edit.changed()) {
        ctx.commands.execute(std::make_unique<SetTransformCommand>(node.id(), t, edit.undoName()));
    }
    edit.finish();
}

// ---------------------------------------------------------------------------
// Kind specific part

void InspectorPanel::drawContent(EditorContext& ctx, const Node& node, EditSession& edit, NodeData& data) {
    const std::string heading = kindLabel(data.kind());
    ImGui::SeparatorText(heading.c_str());

    if (auto* c = data.as<PrimitiveContent>()) {
        editors::primitive(edit, *c);
    } else if (data.as<ModelContent>() != nullptr) {
        drawModelFile(ctx, node, edit, data);
    } else if (auto* t = data.as<TrussContent>()) {
        editors::truss(edit, *t);
    } else if (auto* deck = data.as<StageDeckContent>()) {
        editors::stageDeck(edit, *deck);
    } else if (auto* steps = data.as<StepsContent>()) {
        editors::steps(edit, *steps);
    } else if (auto* wall = data.as<WallContent>()) {
        editors::wall(edit, *wall);
    } else if (auto* fixture = data.as<FixtureContent>()) {
        editors::fixture(edit, ctx.fixtures, *fixture);
    } else if (data.as<CameraPresetContent>() != nullptr) {
        drawCameraPreset(ctx, node, edit, data);
    } else if (auto* figure = data.as<ReferenceFigureContent>()) {
        editors::referenceFigure(edit, *figure);
    } else if (auto* unknown = data.as<UnknownContent>()) {
        ImGui::TextWrapped("Kind \"%s\" comes from a newer DmxViz version. It is kept in the project but not shown.",
                           unknown->kind.c_str());
    } else {
        ImGui::TextDisabled("%zu child node(s)", node.children().size());
    }

    if (hasGeometry(data.kind())) drawMaterialOverride(edit, data);
}

void InspectorPanel::drawMaterialOverride(EditSession& edit, NodeData& data) {
    ImGui::SeparatorText("Material override");
    bool enabled = data.materialOverride.has_value();
    if (edit.touch(ImGui::Checkbox("Replace all materials", &enabled), "Material override"))
        data.materialOverride = enabled ? std::optional<Material>(Material{}) : std::nullopt;
    if (data.materialOverride) editors::material(edit, "Override", *data.materialOverride);
}

void InspectorPanel::drawModelFile(EditorContext& ctx, const Node& node, EditSession& edit, NodeData& data) {
    ModelContent* model = data.as<ModelContent>();
    if (model == nullptr) return;

    char path[1024];
    std::snprintf(path, sizeof(path), "%s", model->path.c_str());
    ImGui::InputText("File", path, sizeof(path), ImGuiInputTextFlags_ReadOnly);
    ImGui::SetItemTooltip("%s", model->path.c_str());

    ImGui::BeginDisabled(!FileDialogs::available() || dialogs_.busy());
    if (ImGui::Button("Browse...")) {
        modelDialogNode_ = node.id();
        dialogs_.requestOpen("Choose a 3D model", pathFromUtf8(model->path).parent_path(),
                             {"3D models", "*.glb *.gltf *.obj *.3ds", "All files", "*"});
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    // Reloading drops every cached model, so changed files are read again.
    if (ImGui::Button("Reload from disk")) ctx.scene.invalidateGeometry();

    const LoadedModel* loaded = ctx.scene.models().find(*model);
    if (loaded != nullptr && !loaded->ok())
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", loaded->error.c_str());
    else if (loaded != nullptr)
        ImGui::TextDisabled("%zu mesh part(s), %.2f x %.2f x %.2f m", loaded->parts.size(), loaded->bounds.size().x,
                            loaded->bounds.size().y, loaded->bounds.size().z);

    editors::modelSettings(edit, *model);
}

void InspectorPanel::pollModelDialog(EditorContext& ctx) {
    const std::optional<FileDialogs::Result> result = dialogs_.poll();
    if (!result || result->path.empty()) return;
    const Node* node = ctx.scene.find(modelDialogNode_);
    if (node == nullptr || node->as<ModelContent>() == nullptr) return;

    const std::string newPath = pathToUtf8(result->path);
    ctx.commands.execute(makeEditCommand(ctx.scene, {modelDialogNode_}, "Set model file", [&](NodeData& d) {
        if (ModelContent* m = d.as<ModelContent>()) m->path = newPath;
    }));
    ctx.commands.breakMerge();
}

void InspectorPanel::drawCameraPreset(EditorContext& ctx, const Node& node, EditSession& edit, NodeData& data) {
    CameraPresetContent* preset = data.as<CameraPresetContent>();
    if (preset == nullptr) return;
    editors::cameraPreset(edit, *preset);
    if (ctx.viewportCamera == nullptr) return;

    // The preset's pose: the node looks along its local -Z.
    const glm::mat4 world = ctx.scene.worldMatrix(node.id());
    if (ImGui::Button("Apply to viewport")) {
        const glm::vec3 eye = glm::vec3(world[3]);
        const glm::vec3 forward = glm::normalize(glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
        ctx.viewportCamera->lookFromTo(eye, eye + forward * preset->targetDistance);
        ctx.viewportCamera->setFovY(degToRad(preset->fovYDeg));
    }
    ImGui::SameLine();
    if (ImGui::Button("Set from viewport")) {
        const ViewportCamera& cam = *ctx.viewportCamera;
        const Transform worldPose = Transform::lookAt(cam.position(), cam.target());
        const Transform local = ctx.scene.localFromWorld(node.parent(), worldPose.matrix());
        const float fov = radToDeg(cam.fovY());
        const float distance = cam.distance();
        // Pose and lens change together, so they are one undo step.
        ctx.commands.beginBatch("Set camera preset from viewport");
        ctx.commands.execute(std::make_unique<SetTransformCommand>(node.id(), local, "Camera pose"));
        ctx.commands.execute(makeEditCommand(ctx.scene, {node.id()}, "Camera lens", [&](NodeData& d) {
            if (CameraPresetContent* c = d.as<CameraPresetContent>()) {
                c->fovYDeg = fov;
                c->targetDistance = distance;
            }
        }));
        ctx.commands.endBatch();
    }
}

// ---------------------------------------------------------------------------
// Several nodes

void InspectorPanel::drawMultiple(EditorContext& ctx) {
    const std::vector<NodeId>& ids = ctx.selection.ids();
    ImGui::Text("%zu nodes selected", ids.size());
    if (const Node* primary = ctx.scene.find(ctx.selection.primary()))
        ImGui::TextDisabled("Primary: %s", primary->name().c_str());

    ImGui::SeparatorText("All selected");
    if (ImGui::Button("Show")) actions::setVisible(ctx, ids, true);
    ImGui::SameLine();
    if (ImGui::Button("Hide")) actions::setVisible(ctx, ids, false);
    ImGui::SameLine();
    if (ImGui::Button("Lock")) actions::setLocked(ctx, ids, true);
    ImGui::SameLine();
    if (ImGui::Button("Unlock")) actions::setLocked(ctx, ids, false);

    // "Move by" works on offsets: the field always shows 0 and each change moves everything by that amount.
    EditSession moveEdit(ctx);
    glm::vec3 delta(0.0f);
    if (moveEdit.touch(ImGui::DragFloat3("Move by (m)", &delta.x, 0.01f, 0.0f, 0.0f, "%.3f"), "Move by", "Move") &&
        delta != glm::vec3(0.0f)) {
        std::vector<std::pair<NodeId, Transform>> targets;
        for (NodeId id : actions::editableTopLevel(ctx, ids))
            targets.emplace_back(id, tools::translatedInWorld(ctx.scene, id, delta));
        if (!targets.empty()) ctx.commands.execute(std::make_unique<SetTransformCommand>(std::move(targets), "Move"));
    }
    moveEdit.finish();

    // Colour: every selected node gets a material override with this albedo.
    EditSession paint(ctx);
    const Node* primary = ctx.scene.find(ctx.selection.primary());
    glm::vec3 albedo(0.5f);
    if (primary != nullptr && primary->data().materialOverride) albedo = primary->data().materialOverride->albedo;
    if (paint.touch(colorEditLinear("Colour", albedo, ImGuiColorEditFlags_NoInputs), "Colour")) {
        ctx.commands.execute(makeEditCommand(
            ctx.scene, ids, "Set Colour",
            [albedo](NodeData& d) {
                if (!hasGeometry(d.kind())) return;
                Material m = d.materialOverride.value_or(Material{});
                m.albedo = albedo;
                d.materialOverride = m;
            },
            "Batch colour"));
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear colour override")) {
        ctx.commands.execute(
            makeEditCommand(ctx.scene, ids, "Clear colour override", [](NodeData& d) { d.materialOverride.reset(); }));
    }
    paint.finish();
}

}  // namespace dmxviz::ui
