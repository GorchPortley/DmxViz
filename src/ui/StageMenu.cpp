#include "ui/StageMenu.h"

#include "core/Log.h"
#include "stage/Commands.h"
#include "stage/NodeFactory.h"
#include "stage/PathUtil.h"
#include "stage/PlacementTools.h"
#include "ui/EditorContext.h"
#include "ui/NodeActions.h"
#include "ui/ViewportCamera.h"

#include "imgui.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace dmxviz::ui {

using namespace stage;

namespace {

constexpr float kHungTrussHeight = 3.0f;  // metres: horizontal truss is added at lighting height
constexpr int kMaxCopies = 2000;          // sanity limit for the array tools
constexpr const char* kAxisNames[] = {"X", "Y", "Z"};

const char* dialogTitle(int index) {
    static const char* const kTitles[] = {
        "", "Add truss", "Linear array", "Grid array", "Circular array", "Mirror", "Hang fixtures on truss"};
    return kTitles[index];
}

const char* const kTrussPieces[] = {"Straight run", "Corner block", "Arc", "Full circle", "Ground support tower"};

struct CornerChoice {
    TrussCornerPreset preset;
    const char* name;
};
const CornerChoice kCornerChoices[] = {
    {TrussCornerPreset::TwoWay, "2-way corner"},
    {TrussCornerPreset::ThreeWayT, "3-way T"},
    {TrussCornerPreset::ThreeWayCorner, "3-way corner"},
    {TrussCornerPreset::FourWayCross, "4-way cross"},
    {TrussCornerPreset::FourWayT, "4-way T"},
    {TrussCornerPreset::FiveWay, "5-way"},
    {TrussCornerPreset::SixWay, "6-way"},
};

struct Facing {
    const char* name;
    glm::vec3 direction;
};
const Facing kFacings[] = {
    {"Toward +Z (audience)", {0.0f, 0.0f, 1.0f}},
    {"Toward -Z (upstage)", {0.0f, 0.0f, -1.0f}},
    {"Toward +X", {1.0f, 0.0f, 0.0f}},
    {"Toward -X", {-1.0f, 0.0f, 0.0f}},
};

// Nodes a tool may change: the selection without locked nodes and without children of selected nodes.
std::vector<NodeId> toolTargets(const EditorContext& ctx) {
    return actions::editableTopLevel(ctx, ctx.selection.ids());
}

// Executes a tool command. Tools that create nodes leave originals plus copies selected.
void runTool(EditorContext& ctx, std::unique_ptr<Command> command, const std::vector<NodeId>& originals) {
    if (!command) return;
    Command* done = ctx.commands.execute(std::move(command));
    if (done == nullptr) return;
    std::vector<NodeId> created = done->resultNodes();
    if (!created.empty()) {
        std::vector<NodeId> selected = originals;
        selected.insert(selected.end(), created.begin(), created.end());
        ctx.selection.setMany(selected);
    }
    ctx.selection.prune(ctx.scene);
}

glm::vec3 toVec3(const float (&v)[3]) {
    return {v[0], v[1], v[2]};
}

void selectionInfo(std::size_t count) {
    ImGui::TextDisabled("%zu node(s) selected", count);
}

bool okCancelButtons(bool canApply, const char* applyLabel, bool& applied) {
    ImGui::Separator();
    ImGui::BeginDisabled(!canApply);
    const float buttonWidth = ImGui::GetFontSize() * 7.0f;
    applied = ImGui::Button(applyLabel, ImVec2(buttonWidth, 0.0f));
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool cancelled = ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0f));
    return applied || cancelled;
}

// Name of a model file without folder and extension, for the new node.
std::string modelNodeName(const std::filesystem::path& file) {
    const std::string stem = pathToUtf8(file.stem());
    return stem.empty() ? "Model" : stem;
}

}  // namespace

void drawStageMenu(EditorContext& ctx) {
    static StageMenu menu;
    menu.draw(ctx);
}

// ---------------------------------------------------------------------------

void StageMenu::draw(EditorContext& ctx) {
    pollImportDialog(ctx);
    drawAddMenu(ctx);
    drawToolsMenu(ctx);
    showDialogs(ctx);
}

// ---------------------------------------------------------------------------
// Add menu

void StageMenu::drawAddMenu(EditorContext& ctx) {
    if (!ImGui::BeginMenu("Add")) return;

    if (ImGui::BeginMenu("Primitive")) {
        struct Entry {
            PrimitiveShape shape;
            const char* label;
            glm::vec3 size;
        };
        static const Entry kEntries[] = {
            {PrimitiveShape::Box, "Box", {1.0f, 1.0f, 1.0f}},
            {PrimitiveShape::Cylinder, "Cylinder", {0.5f, 1.0f, 0.5f}},
            {PrimitiveShape::Sphere, "Sphere", {1.0f, 1.0f, 1.0f}},
            {PrimitiveShape::Plane, "Plane", {4.0f, 1.0f, 4.0f}},
            {PrimitiveShape::Cone, "Cone", {0.6f, 1.0f, 0.6f}},
            {PrimitiveShape::Disc, "Disc", {1.0f, 1.0f, 1.0f}},
        };
        for (const Entry& e : kEntries) {
            if (!ImGui::MenuItem(e.label)) continue;
            // Box, cylinder and sphere are centred on their origin: lift them so they rest on the floor.
            const bool centred = e.shape == PrimitiveShape::Box || e.shape == PrimitiveShape::Cylinder ||
                                 e.shape == PrimitiveShape::Sphere;
            actions::addNodeAtPlacement(ctx, factory::primitive(e.shape, e.size), centred ? e.size.y * 0.5f : 0.0f);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Truss")) {
        for (int i = 0; i < static_cast<int>(std::size(kTrussPieces)); ++i) {
            const std::string label = std::string(kTrussPieces[i]) + "...";
            if (ImGui::MenuItem(label.c_str())) {
                truss_.piece = i;
                openDialog(Dialog::Truss);
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Stage")) {
        if (ImGui::MenuItem("Deck (2 x 1 m)")) actions::addNodeAtPlacement(ctx, factory::stageDeck(1, 1, 0.6f));
        if (ImGui::MenuItem("Riser")) actions::addNodeAtPlacement(ctx, factory::riser(1, 1, 0.4f));
        if (ImGui::MenuItem("Steps")) actions::addNodeAtPlacement(ctx, factory::steps(1.0f, 0.6f));
        ImGui::Separator();
        if (ImGui::MenuItem("Wall")) actions::addNodeAtPlacement(ctx, factory::wall(4.0f, 3.0f));
        if (ImGui::MenuItem("Flat")) actions::addNodeAtPlacement(ctx, factory::flat(1.2f, 2.4f));
        if (ImGui::MenuItem("Floor")) actions::addNode(ctx, factory::floor(), Transform{});
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem("Reference figure")) actions::addNodeAtPlacement(ctx, factory::referenceFigure());
    if (ImGui::MenuItem("Group")) actions::addNodeAtPlacement(ctx, factory::group());

    ImGui::Separator();
    if (ImGui::MenuItem("Camera preset from view", nullptr, false, ctx.viewportCamera != nullptr)) {
        const ViewportCamera& cam = *ctx.viewportCamera;
        const std::size_t number = ctx.scene.nodesOfKind(NodeKind::CameraPreset).size() + 1;
        NodeData data = factory::cameraPreset("Camera " + std::to_string(number), radToDeg(cam.fovY()));
        if (CameraPresetContent* preset = data.as<CameraPresetContent>()) preset->targetDistance = cam.distance();
        actions::addNode(ctx, std::move(data), Transform::lookAt(cam.position(), cam.target()));
    }

    ImGui::Separator();
    const bool canImport = FileDialogs::available() && !fileDialogs_.busy();
    if (ImGui::MenuItem("Import model...", "glTF, GLB, OBJ, 3DS", false, canImport)) {
        fileDialogs_.requestOpen("Import 3D model", lastModelDir_,
                                 {"3D models", "*.glb *.gltf *.obj *.3ds", "All files", "*"});
    }
    ImGui::EndMenu();
}

void StageMenu::pollImportDialog(EditorContext& ctx) {
    const std::optional<FileDialogs::Result> result = fileDialogs_.poll();
    if (!result || result->path.empty()) return;
    lastModelDir_ = result->path.parent_path();

    NodeData data = factory::model(pathToUtf8(result->path));
    data.name = modelNodeName(result->path);
    const NodeId id = actions::addNodeAtPlacement(ctx, std::move(data));
    log::info("ui", "imported model {} as node {}", pathToUtf8(result->path), id);
}

// ---------------------------------------------------------------------------
// Tools menu

void StageMenu::drawToolsMenu(EditorContext& ctx) {
    if (!ImGui::BeginMenu("Tools")) return;

    const std::vector<NodeId> targets = toolTargets(ctx);
    const bool any = !targets.empty();
    const bool two = targets.size() >= 2;
    const bool three = targets.size() >= 3;

    if (ImGui::MenuItem("Group selection", "Ctrl+G", false, any)) actions::groupNodes(ctx, targets);
    ImGui::Separator();

    if (ImGui::MenuItem("Linear array...", nullptr, false, any)) openDialog(Dialog::LinearArray);
    if (ImGui::MenuItem("Grid array...", nullptr, false, any)) openDialog(Dialog::GridArray);
    if (ImGui::MenuItem("Circular array...", nullptr, false, any)) openDialog(Dialog::CircularArray);
    ImGui::Separator();

    if (ImGui::BeginMenu("Align", two)) {
        static const char* const kModes[] = {"Min", "Centre", "Max"};
        for (int axis = 0; axis < 3; ++axis) {
            if (!ImGui::BeginMenu(kAxisNames[axis])) continue;
            for (int mode = 0; mode < 3; ++mode) {
                if (!ImGui::MenuItem(kModes[mode])) continue;
                runTool(ctx,
                        tools::alignNodes(ctx.scene, ctx.assets, targets, static_cast<tools::Axis>(axis),
                                          static_cast<tools::AlignMode>(mode)),
                        targets);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Distribute", three)) {
        for (int axis = 0; axis < 3; ++axis) {
            if (!ImGui::BeginMenu(kAxisNames[axis])) continue;
            if (ImGui::MenuItem("Equal distance between centres"))
                runTool(ctx,
                        tools::distributeNodes(ctx.scene, ctx.assets, targets, static_cast<tools::Axis>(axis),
                                               tools::DistributeMode::Centres),
                        targets);
            if (ImGui::MenuItem("Equal gaps"))
                runTool(ctx,
                        tools::distributeNodes(ctx.scene, ctx.assets, targets, static_cast<tools::Axis>(axis),
                                               tools::DistributeMode::Gaps),
                        targets);
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Mirror...", nullptr, false, any)) openDialog(Dialog::Mirror);
    if (ImGui::MenuItem("Drop to floor", nullptr, false, any)) {
        // The meshes of the current frame are the surfaces the nodes may land on.
        runTool(ctx, tools::dropToFloor(ctx.scene, ctx.assets, targets, 0.0f, &ctx.frame.meshes), targets);
    }
    ImGui::Separator();

    // Hanging needs one truss and at least one fixture in the selection.
    std::size_t fixtureCount = 0;
    std::size_t trussCount = 0;
    for (NodeId id : ctx.selection.ids()) {
        const Node* node = ctx.scene.find(id);
        if (node == nullptr) continue;
        if (node->kind() == NodeKind::Fixture) ++fixtureCount;
        if (node->kind() == NodeKind::Truss) ++trussCount;
    }
    const bool canHang = fixtureCount > 0 && trussCount == 1;
    if (ImGui::MenuItem("Hang fixtures on truss...", nullptr, false, canHang)) openDialog(Dialog::Hang);
    if (!canHang) ImGui::SetItemTooltip("Select the fixtures and exactly one truss.");
    ImGui::EndMenu();
}

// ---------------------------------------------------------------------------
// Dialogs

void StageMenu::showDialogs(EditorContext& ctx) {
    if (requested_ != Dialog::None) {
        active_ = requested_;
        requested_ = Dialog::None;
        ImGui::OpenPopup(dialogTitle(static_cast<int>(active_)));
    }
    if (active_ == Dialog::None) return;

    const char* title = dialogTitle(static_cast<int>(active_));
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    bool open = true;
    if (ImGui::BeginPopupModal(title, &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        bool close = ImGui::IsKeyPressed(ImGuiKey_Escape);
        switch (active_) {
            case Dialog::Truss:
                close = trussDialog(ctx) || close;
                break;
            case Dialog::LinearArray:
                close = linearArrayDialog(ctx) || close;
                break;
            case Dialog::GridArray:
                close = gridArrayDialog(ctx) || close;
                break;
            case Dialog::CircularArray:
                close = circularArrayDialog(ctx) || close;
                break;
            case Dialog::Mirror:
                close = mirrorDialog(ctx) || close;
                break;
            case Dialog::Hang:
                close = hangDialog(ctx) || close;
                break;
            case Dialog::None:
                break;
        }
        if (close) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!open || !ImGui::IsPopupOpen(title)) active_ = Dialog::None;
}

bool StageMenu::trussDialog(EditorContext& ctx) {
    ImGui::Combo("Piece", &truss_.piece, kTrussPieces, static_cast<int>(std::size(kTrussPieces)));

    const std::vector<TrussProfile>& profiles = standardTrussProfiles();
    truss_.profile = std::clamp(truss_.profile, 0, static_cast<int>(profiles.size()) - 1);
    const TrussProfile& profile = profiles[static_cast<std::size_t>(truss_.profile)];
    if (ImGui::BeginCombo("Profile", profile.name.c_str())) {
        for (std::size_t i = 0; i < profiles.size(); ++i)
            if (ImGui::Selectable(profiles[i].name.c_str(), static_cast<int>(i) == truss_.profile))
                truss_.profile = static_cast<int>(i);
        ImGui::EndCombo();
    }

    switch (truss_.piece) {
        case 0:
            ImGui::InputFloat("Length (m)", &truss_.length, 0.5f, 1.0f, "%.2f");
            truss_.length = std::clamp(truss_.length, 0.1f, 100.0f);
            break;
        case 1:
            if (ImGui::BeginCombo("Block type", kCornerChoices[truss_.cornerPreset].name)) {
                for (int i = 0; i < static_cast<int>(std::size(kCornerChoices)); ++i)
                    if (ImGui::Selectable(kCornerChoices[i].name, i == truss_.cornerPreset)) truss_.cornerPreset = i;
                ImGui::EndCombo();
            }
            break;
        case 2:
            ImGui::InputFloat("Radius (m)", &truss_.radius, 0.25f, 1.0f, "%.2f");
            ImGui::InputFloat("Angle (deg)", &truss_.angleDeg, 5.0f, 45.0f, "%.1f");
            ImGui::InputInt("Pieces", &truss_.arcPieces);
            truss_.radius = std::clamp(truss_.radius, 0.2f, 100.0f);
            truss_.angleDeg = std::clamp(truss_.angleDeg, 1.0f, 360.0f);
            truss_.arcPieces = std::clamp(truss_.arcPieces, 1, 64);
            break;
        case 3:
            ImGui::InputFloat("Radius (m)", &truss_.radius, 0.25f, 1.0f, "%.2f");
            ImGui::InputInt("Pieces", &truss_.circlePieces);
            truss_.radius = std::clamp(truss_.radius, 0.2f, 100.0f);
            truss_.circlePieces = std::clamp(truss_.circlePieces, 1, 64);
            break;
        default:
            ImGui::InputFloat("Height (m)", &truss_.towerHeight, 0.5f, 1.0f, "%.2f");
            ImGui::InputFloat("Sleeve height (m)", &truss_.sleeveHeight, 0.5f, 1.0f, "%.2f");
            truss_.towerHeight = std::clamp(truss_.towerHeight, 0.5f, 50.0f);
            truss_.sleeveHeight = std::clamp(truss_.sleeveHeight, 0.3f, truss_.towerHeight);
            break;
    }
    ImGui::TextDisabled("Placed in front of the camera, %s.",
                        truss_.piece == 4 ? "standing on the floor" : "at lighting height");

    bool add = false;
    const bool close = okCancelButtons(true, "Add", add);
    if (!add) return close;

    NodeData data;
    switch (truss_.piece) {
        case 0:
            data = factory::trussStraight(profile, truss_.length);
            break;
        case 1:
            data = factory::trussCorner(profile, kCornerChoices[truss_.cornerPreset].preset);
            break;
        case 2:
            data = factory::trussArc(profile, truss_.radius, truss_.angleDeg, truss_.arcPieces);
            break;
        case 3:
            data = factory::trussCircle(profile, truss_.radius, truss_.circlePieces);
            break;
        default:
            data = factory::trussTower(profile, truss_.towerHeight, truss_.sleeveHeight);
            break;
    }
    actions::addNodeAtPlacement(ctx, std::move(data), truss_.piece == 4 ? 0.0f : kHungTrussHeight);
    return true;
}

bool StageMenu::linearArrayDialog(EditorContext& ctx) {
    const std::vector<NodeId> targets = toolTargets(ctx);
    selectionInfo(targets.size());
    ImGui::InputInt("Copies", &linear_.copies);
    linear_.copies = std::clamp(linear_.copies, 1, kMaxCopies);
    ImGui::InputFloat3("Offset per copy (m)", linear_.offset, "%.3f");

    bool apply = false;
    const bool close = okCancelButtons(!targets.empty(), "Create", apply);
    if (apply) runTool(ctx, tools::linearArray(ctx.scene, targets, linear_.copies, toVec3(linear_.offset)), targets);
    return close;
}

bool StageMenu::gridArrayDialog(EditorContext& ctx) {
    const std::vector<NodeId> targets = toolTargets(ctx);
    selectionInfo(targets.size());
    ImGui::InputInt3("Count X, Y, Z", grid_.counts);
    for (int& count : grid_.counts) count = std::clamp(count, 1, kMaxCopies);
    ImGui::InputFloat3("Spacing (m)", grid_.spacing, "%.3f");

    const long long total = static_cast<long long>(grid_.counts[0]) * grid_.counts[1] * grid_.counts[2];
    const bool tooMany = total * static_cast<long long>(std::max<std::size_t>(targets.size(), 1)) > kMaxCopies;
    ImGui::TextDisabled("%lld cell(s), including the original", total);
    if (tooMany)
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "That would create more than %d nodes.", kMaxCopies);

    bool apply = false;
    const bool close = okCancelButtons(!targets.empty() && !tooMany, "Create", apply);
    if (apply) {
        const glm::ivec3 counts(grid_.counts[0], grid_.counts[1], grid_.counts[2]);
        runTool(ctx, tools::gridArray(ctx.scene, targets, counts, toVec3(grid_.spacing)), targets);
    }
    return close;
}

bool StageMenu::circularArrayDialog(EditorContext& ctx) {
    const std::vector<NodeId> targets = toolTargets(ctx);
    selectionInfo(targets.size());
    ImGui::InputInt("Items (with original)", &circular_.count);
    circular_.count = std::clamp(circular_.count, 2, kMaxCopies);
    ImGui::InputFloat("Total angle (deg)", &circular_.totalAngleDeg, 15.0f, 90.0f, "%.1f");
    circular_.totalAngleDeg = std::clamp(circular_.totalAngleDeg, 1.0f, 360.0f);
    ImGui::Combo("Axis", &circular_.axis, kAxisNames, 3);
    ImGui::InputFloat3("Centre (m)", circular_.centre, "%.3f");
    if (ImGui::SmallButton("Origin")) std::fill(std::begin(circular_.centre), std::end(circular_.centre), 0.0f);
    ImGui::SameLine();
    if (ImGui::SmallButton("Viewport target")) {
        const glm::vec3 p = actions::placementPoint(ctx);
        circular_.centre[0] = p.x;
        circular_.centre[1] = p.y;
        circular_.centre[2] = p.z;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Selection centre") && !targets.empty()) {
        const Aabb bounds = tools::selectionBounds(ctx.scene, ctx.assets, targets);
        if (!bounds.empty()) {
            const glm::vec3 c = bounds.center();
            circular_.centre[0] = c.x;
            circular_.centre[1] = c.y;
            circular_.centre[2] = c.z;
        }
    }
    ImGui::Checkbox("Turn copies with the circle", &circular_.rotateCopies);
    ImGui::TextDisabled("360 deg spaces the items evenly around the full circle.");

    bool apply = false;
    const bool close = okCancelButtons(!targets.empty(), "Create", apply);
    if (apply) {
        tools::CircularArraySettings settings;
        settings.centre = toVec3(circular_.centre);
        settings.axis = glm::vec3(0.0f);
        settings.axis[circular_.axis] = 1.0f;
        settings.count = circular_.count;
        settings.totalAngleDeg = circular_.totalAngleDeg;
        settings.rotateCopies = circular_.rotateCopies;
        runTool(ctx, tools::circularArray(ctx.scene, targets, settings), targets);
    }
    return close;
}

bool StageMenu::mirrorDialog(EditorContext& ctx) {
    const std::vector<NodeId> targets = toolTargets(ctx);
    selectionInfo(targets.size());
    ImGui::Combo("Mirror across the plane", &mirror_.axis, "X = position\0Y = position\0Z = position\0");
    ImGui::InputFloat("Plane position (m)", &mirror_.position, 0.25f, 1.0f, "%.3f");
    ImGui::Checkbox("Keep the originals and add mirrored copies", &mirror_.copy);

    bool apply = false;
    const bool close = okCancelButtons(!targets.empty(), "Mirror", apply);
    if (apply) {
        runTool(ctx,
                tools::mirrorNodes(ctx.scene, targets, static_cast<tools::Axis>(mirror_.axis), mirror_.position,
                                   mirror_.copy),
                targets);
    }
    return close;
}

bool StageMenu::hangDialog(EditorContext& ctx) {
    std::vector<NodeId> fixtureIds;
    NodeId trussId = kInvalidNode;
    for (NodeId id : ctx.selection.ids()) {
        const Node* node = ctx.scene.find(id);
        if (node == nullptr) continue;
        if (node->kind() == NodeKind::Fixture) fixtureIds.push_back(id);
        if (node->kind() == NodeKind::Truss) trussId = id;
    }
    ImGui::TextDisabled("%zu fixture(s) on the selected truss", fixtureIds.size());

    ImGui::Combo("Fixtures face", &hang_.facing, "Toward +Z (audience)\0Toward -Z (upstage)\0Toward +X\0Toward -X\0");
    ImGui::Checkbox("Spread evenly along the truss", &hang_.spread);
    ImGui::SetItemTooltip("Otherwise each fixture is clamped at the nearest point to where it is now.");
    ImGui::InputFloat("Snap along the truss (m, 0 = off)", &hang_.snapAlong, 0.1f, 0.5f, "%.2f");
    hang_.snapAlong = std::max(hang_.snapAlong, 0.0f);
    ImGui::Checkbox("Attach to the truss (moves with it)", &hang_.reparent);

    bool apply = false;
    const bool close = okCancelButtons(!fixtureIds.empty() && trussId != kInvalidNode, "Hang", apply);
    if (!apply) return close;

    tools::HangOptions options;
    options.facing = kFacings[std::clamp(hang_.facing, 0, 3)].direction;
    options.snapAlong = hang_.snapAlong;
    options.reparent = hang_.reparent;

    // Order the fixtures along the truss so a spread keeps their current left-to-right order.
    const Aabb bounds = ctx.scene.worldBounds(trussId, ctx.assets, false);
    const glm::vec3 size = bounds.empty() ? glm::vec3(0.0f) : bounds.size();
    const int axis = size.x >= size.z ? 0 : 2;
    std::sort(fixtureIds.begin(), fixtureIds.end(), [&](NodeId a, NodeId b) {
        return ctx.scene.worldMatrix(a)[3][axis] < ctx.scene.worldMatrix(b)[3][axis];
    });

    auto compound = std::make_unique<CompoundCommand>("Hang on truss");
    for (std::size_t i = 0; i < fixtureIds.size(); ++i) {
        glm::vec3 hitPoint = glm::vec3(ctx.scene.worldMatrix(fixtureIds[i])[3]);
        if (hang_.spread && !bounds.empty()) {
            const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(fixtureIds.size());
            hitPoint = bounds.center();
            hitPoint[axis] = bounds.min[axis] + t * size[axis];
        }
        if (std::unique_ptr<Command> hang =
                tools::hangOnTrussCommand(ctx.scene, fixtureIds[i], trussId, hitPoint, options))
            compound->add(std::move(hang));
    }
    if (!compound->empty()) ctx.commands.execute(std::move(compound));
    return true;
}

}  // namespace dmxviz::ui
