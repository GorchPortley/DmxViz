#include "ui/fixture_editor/GeometrySection.h"

#include "fixtures/ColorMath.h"
#include "fixtures/FixtureAssets.h"
#include "ui/ColorWidgets.h"
#include "ui/fixture_editor/ChannelEditing.h"
#include "ui/fixture_editor/EditorWidgets.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <format>

namespace dmxviz::ui::fixture_editor {

using fixtures::Attribute;
using fixtures::Geometry;
using fixtures::GeometryType;

namespace {

constexpr const char* kTypeNames[] = {"Generic", "Axis", "Beam"};
// Same order as fixtures::PrimitiveShape.
constexpr const char* kShapeNames[] = {"None", "Box", "Cylinder", "Sphere", "Base", "Yoke", "Head", "Conventional"};
// Same order as fixtures::BeamType.
constexpr const char* kBeamNames[] = {"Spot", "Wash", "Beam", "PC", "Fresnel", "Rectangle", "Glow"};
constexpr const char* kDirectionNames[] = {"X (left to right)", "Y (up)", "Z (front to back)"};
constexpr const char* kCellChannelNames[] = {"No channels", "One RGB set for all cells", "One RGB set per cell"};

const char* typeIcon(GeometryType type) {
    switch (type) {
        case GeometryType::Generic: return "[G]";
        case GeometryType::Axis: return "[A]";
        case GeometryType::Beam: return "[B]";
    }
    return "[?]";
}

bool sameQuat(const glm::quat& a, const glm::quat& b) {
    return a.w == b.w && a.x == b.x && a.y == b.y && a.z == b.z;
}

}  // namespace

void GeometrySection::reset() {
    selected_.clear();
    pending_ = Action::None;
    nameEdit_.clear();
    nameEditSource_.clear();
    message_.clear();
}

void GeometrySection::select(const fixtures::FixtureType& type, const std::string& geometryName) {
    if (const auto path = findNodePath(type.geometry, geometryName)) selected_ = *path;
}

// ---------------------------------------------------------------------------
// Frame

bool GeometrySection::draw(EditDocument& doc, int modeIndex) {
    fixtures::FixtureType& type = doc.type();
    bool changed = false;
    pollModelImport(type, changed);
    if (nodeAt(type.geometry, selected_) == nullptr) selected_.clear();

    const float available = ImGui::GetContentRegionAvail().x;
    const float treeWidth = std::clamp(available * 0.36f, 170.0f, 300.0f);

    if (ImGui::BeginChild("##geometryTree", ImVec2(treeWidth, 0.0f), ImGuiChildFlags_Borders)) {
        drawToolbar(type);
        ImGui::Separator();
        NodePath path;
        drawTreeNode(type.geometry, path);
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("##geometryProperties", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        changed |= drawProperties(doc, modeIndex);
    }
    ImGui::EndChild();

    changed |= runAction(type, modeIndex);
    changed |= drawCellsPopup(type, modeIndex);
    return changed;
}

void GeometrySection::drawToolbar(const fixtures::FixtureType& type) {
    const Geometry* node = nodeAt(type.geometry, selected_);
    const bool isRoot = selected_.empty();

    auto button = [&](const char* label, Action action, const char* tooltip, bool enabled = true) {
        ImGui::BeginDisabled(!enabled);
        if (ImGui::SmallButton(label)) pending_ = action;
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("%s", tooltip);
        ImGui::SameLine();
    };
    button("+Generic", Action::AddGeneric, "Add a static part below the selected node", node != nullptr);
    button("+Axis", Action::AddAxis, "Add a part that rotates when driven by Pan or Tilt", node != nullptr);
    button("+Beam", Action::AddBeam, "Add a light source below the selected node", node != nullptr);
    ImGui::NewLine();

    bool canUp = false;
    bool canDown = false;
    if (!isRoot) {
        const NodePath parentPath(selected_.begin(), selected_.end() - 1);
        if (const Geometry* parent = nodeAt(type.geometry, parentPath)) {
            canUp = selected_.back() > 0;
            canDown = selected_.back() + 1 < static_cast<int>(parent->children.size());
        }
    }
    button("Up", Action::MoveUp, "Move the node before its previous sibling", canUp);
    button("Down", Action::MoveDown, "Move the node after its next sibling", canDown);
    button("Remove", Action::Remove, "Delete the node and everything below it", !isRoot);
    button("Make cells...", Action::MakeCells, "Create a row of beam cells below the selected node (pixel bars)",
           node != nullptr);
    ImGui::NewLine();
}

void GeometrySection::drawTreeNode(const Geometry& node, NodePath& path) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (node.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (path == selected_) flags |= ImGuiTreeNodeFlags_Selected;

    char label[160];
    std::snprintf(label, sizeof(label), "%s %s###node", typeIcon(node.type), node.name.empty() ? "(unnamed)" : node.name.c_str());
    const bool open = ImGui::TreeNodeEx(label, flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) selected_ = path;
    if (ImGui::BeginPopupContextItem()) {
        selected_ = path;
        if (ImGui::MenuItem("Add Generic")) pending_ = Action::AddGeneric;
        if (ImGui::MenuItem("Add Axis")) pending_ = Action::AddAxis;
        if (ImGui::MenuItem("Add Beam")) pending_ = Action::AddBeam;
        if (ImGui::MenuItem("Make cells...")) pending_ = Action::MakeCells;
        ImGui::Separator();
        if (ImGui::MenuItem("Remove", nullptr, false, !path.empty())) pending_ = Action::Remove;
        ImGui::EndPopup();
    }
    if (!open) return;
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        path.push_back(static_cast<int>(i));
        drawTreeNode(node.children[i], path);
        path.pop_back();
        ImGui::PopID();
    }
    ImGui::TreePop();
}

// ---------------------------------------------------------------------------
// Structure changes

bool GeometrySection::runAction(fixtures::FixtureType& type, int modeIndex) {
    (void)modeIndex;
    const Action action = pending_;
    pending_ = Action::None;
    switch (action) {
        case Action::None: return false;
        case Action::AddGeneric:
        case Action::AddAxis:
        case Action::AddBeam: {
            const GeometryType kind = action == Action::AddGeneric ? GeometryType::Generic
                                      : action == Action::AddAxis  ? GeometryType::Axis
                                                                   : GeometryType::Beam;
            const std::optional<NodePath> added = addChildNode(type, selected_, kind);
            if (added) selected_ = *added;
            return added.has_value();
        }
        case Action::Remove: {
            if (!removeNode(type, selected_)) return false;
            selected_.pop_back();  // the parent
            return true;
        }
        case Action::MoveUp: return moveNode(type, selected_, -1);
        case Action::MoveDown: return moveNode(type, selected_, +1);
        case Action::MakeCells: openCellsPopup(type); return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Properties

bool GeometrySection::drawProperties(EditDocument& doc, int modeIndex) {
    fixtures::FixtureType& type = doc.type();
    Geometry* node = nodeAt(type.geometry, selected_);
    if (node == nullptr) {
        ImGui::TextDisabled("Select a node in the tree.");
        return false;
    }

    bool changed = false;
    changed |= drawName(type, *node);

    int kind = static_cast<int>(node->type);
    if (comboIndex("Type", kind, kTypeNames)) {
        node->type = static_cast<GeometryType>(kind);
        changed = true;
    }
    ImGui::SetItemTooltip("Generic: static part. Axis: rotates when driven by Pan/Tilt. Beam: a light source.");

    ImGui::SeparatorText("Transform");
    changed |= drawTransform(*node);

    ImGui::SeparatorText("Model");
    changed |= drawModel(type, *node);

    if (node->type == GeometryType::Axis) {
        ImGui::SeparatorText("Axis");
        changed |= drawAxis(type, *node, modeIndex);
    }
    if (node->type == GeometryType::Beam) {
        ImGui::SeparatorText("Beam");
        changed |= drawBeam(*node);
    }

    if (!message_.empty()) ImGui::TextColored(errorColor(), "%s", message_.c_str());
    return changed;
}

bool GeometrySection::drawName(fixtures::FixtureType& type, Geometry& node) {
    if (nameEditPath_ != selected_ || nameEditSource_ != node.name) {
        nameEdit_ = node.name;
        nameEditSource_ = node.name;
        nameEditPath_ = selected_;
    }
    const bool valid = !nameEdit_.empty() && (nameEdit_ == node.name || uniqueGeometryName(type, nameEdit_) == nameEdit_);
    if (!valid) ImGui::PushStyleColor(ImGuiCol_Text, errorColor());
    ImGui::InputText("Name", &nameEdit_);
    if (!valid) ImGui::PopStyleColor();
    ImGui::SetItemTooltip("Channels refer to nodes by name; renaming updates them.");

    bool changed = false;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        if (valid && nameEdit_ != node.name) {
            changed = renameNode(type, selected_, nameEdit_);
        } else if (!valid) {
            nameEdit_ = node.name;  // give up: empty or already used
        }
    }
    return changed;
}

bool GeometrySection::drawTransform(Geometry& node) {
    bool changed = dragVec3("Position", node.position, 0.001f, -20.0f, 20.0f, "%.3f m");

    if (eulerPath_ != selected_ || !sameQuat(eulerSource_, node.rotation)) {
        eulerDegrees_ = glm::degrees(glm::eulerAngles(node.rotation));
        eulerSource_ = node.rotation;
        eulerPath_ = selected_;
    }
    if (ImGui::DragFloat3("Rotation", &eulerDegrees_.x, 0.5f, -360.0f, 360.0f, "%.1f deg")) {
        node.rotation = glm::quat(glm::radians(eulerDegrees_));
        eulerSource_ = node.rotation;
        changed = true;
    }
    return changed;
}

bool GeometrySection::drawModel(fixtures::FixtureType& type, Geometry& node) {
    bool changed = false;
    fixtures::ModelSpec& model = node.model;

    int shape = static_cast<int>(model.primitive);
    if (comboIndex("Shape", shape, kShapeNames)) {
        model.primitive = static_cast<fixtures::PrimitiveShape>(shape);
        // A new visible shape without size would be invisible: start from a handy size.
        if (model.primitive != fixtures::PrimitiveShape::None && model.size == glm::vec3(0.0f)) model.size = glm::vec3(0.1f);
        changed = true;
    }
    ImGui::SetItemTooltip("Drawn when there is no mesh file (or it cannot be loaded).");
    changed |= dragVec3("Size", model.size, 0.002f, 0.0f, 10.0f, "%.3f m");
    changed |= colorEditLinear("Color", model.color, ImGuiColorEditFlags_NoInputs);

    // Mesh files are stored inside the fixture, so it stays one self-contained file.
    const char* preview = model.mesh.empty() ? "(none: use the shape)" : model.mesh.c_str();
    if (ImGui::BeginCombo("Mesh file", preview)) {
        if (ImGui::Selectable("(none: use the shape)", model.mesh.empty())) {
            model.mesh.clear();
            removeUnusedResources(type);
            changed = true;
        }
        for (const fixtures::Resource& r : type.resources) {
            if (!fixtures::isModelFormat(r.format)) continue;
            if (ImGui::Selectable(std::format("{}.{}", r.name, r.format).c_str(), r.name == model.mesh)) {
                model.mesh = r.name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(!FileDialogs::available() || dialogs_.busy());
    if (ImGui::SmallButton("Import mesh file...")) {
        importTarget_ = selected_;
        dialogs_.requestOpen("Import 3D model", lastImportDir_, {"3D models (GLB, OBJ, 3DS)", "*.glb *.gltf *.obj *.3ds", "All files", "*"});
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Stores the file in the fixture. Prefer .glb: a .gltf needs to be self-contained.");
    return changed;
}

bool GeometrySection::drawAxis(fixtures::FixtureType& type, const Geometry& node, int modeIndex) {
    bool changed = false;
    if (modeIndex < 0 || modeIndex >= static_cast<int>(type.modes.size())) {
        ImGui::TextDisabled("Add a DMX mode to drive this axis.");
        return false;
    }
    fixtures::DmxMode& mode = type.modes[static_cast<std::size_t>(modeIndex)];

    const std::vector<AxisDrive> drives = axisDrives(type, mode, node.name);
    if (drives.empty()) {
        ImGui::TextColored(warningColor(), "Not driven by any channel in mode \"%s\".", mode.name.c_str());
    } else {
        for (const AxisDrive& drive : drives)
            ImGui::BulletText("%s, by channel \"%s\" (mode \"%s\")", std::string(fixtures::attributeName(drive.attribute)).c_str(),
                              drive.channel.c_str(), mode.name.c_str());
    }

    auto addChannel = [&](Attribute attribute, const char* name) {
        std::vector<std::string> names;
        for (const fixtures::Channel& c : mode.channels) names.push_back(c.name);
        fixtures::Channel channel = makeChannel(uniqueName(name, names), attribute, 2, 1, node.name);
        channel.defaultValue = maxDmxValue(2) / 2 + 1;  // centre position
        appendChannel(mode, std::move(channel));
        changed = true;
    };
    if (ImGui::SmallButton("Add Pan channel")) addChannel(Attribute::Pan, "Pan");
    ImGui::SameLine();
    if (ImGui::SmallButton("Add Tilt channel")) addChannel(Attribute::Tilt, "Tilt");
    ImGui::SetItemTooltip("Adds a 16-bit channel to the selected mode that controls this node. Pan turns the first axis\n"
                          "below the channel's node, Tilt the second one (or the same if there is only one).");
    return changed;
}

bool GeometrySection::drawBeam(Geometry& node) {
    fixtures::BeamSpec& beam = node.beam;
    bool changed = false;

    int type = static_cast<int>(beam.type);
    if (comboIndex("Shape", type, kBeamNames)) {
        beam.type = static_cast<fixtures::BeamType>(type);
        changed = true;
    }
    changed |= ImGui::DragFloat("Lens radius", &beam.lensRadius, 0.001f, 0.001f, 1.0f, "%.3f m");

    float beamDeg = radToDeg(beam.beamAngle);
    if (ImGui::DragFloat("Beam angle", &beamDeg, 0.1f, 0.5f, 170.0f, "%.1f deg")) {
        beam.beamAngle = degToRad(beamDeg);
        beam.fieldAngle = std::max(beam.fieldAngle, beam.beamAngle);  // the field is never narrower than the beam
        changed = true;
    }
    ImGui::SetItemTooltip("Full angle where the intensity falls to 50 %% (before zoom).");
    float fieldDeg = radToDeg(beam.fieldAngle);
    if (ImGui::DragFloat("Field angle", &fieldDeg, 0.1f, 0.5f, 175.0f, "%.1f deg")) {
        beam.fieldAngle = std::max(degToRad(fieldDeg), beam.beamAngle);
        changed = true;
    }
    ImGui::SetItemTooltip("Full angle where the intensity falls to 10 %%.");

    changed |= ImGui::DragFloat("Luminous flux", &beam.luminousFlux, 10.0f, 0.0f, 200000.0f, "%.0f lm");
    changed |= ImGui::DragFloat("Color temperature", &beam.colorTemperature, 25.0f, 1500.0f, 12000.0f, "%.0f K");
    ImGui::SameLine();
    const glm::vec3 white = linearToSrgb(fixtures::kelvinToLinear(beam.colorTemperature));
    ImGui::ColorButton("##kelvin", ImVec4(white.x, white.y, white.z, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(18.0f, 18.0f));

    if (beam.type == fixtures::BeamType::Rectangle)
        changed |= ImGui::DragFloat2("Emitter size", &beam.emitterSize.x, 0.002f, 0.001f, 5.0f, "%.3f m");
    return changed;
}

// ---------------------------------------------------------------------------
// Make cells

void GeometrySection::openCellsPopup(const fixtures::FixtureType& type) {
    const Geometry* parent = nodeAt(type.geometry, selected_);
    if (parent == nullptr) return;
    cells_ = CellOptions{};
    cells_.count = 8;
    cells_.namePrefix = "Cell";
    cells_.pitch = parent->model.size.x > 0.0f ? parent->model.size.x * 0.9f / static_cast<float>(cells_.count) : 0.1f;
    cells_.centre = glm::vec3(0.0f, -parent->model.size.y * 0.5f, 0.0f);
    cells_.beam.type = fixtures::BeamType::Rectangle;
    cells_.beam.lensRadius = 0.02f;
    cells_.beam.beamAngle = degToRad(30.0f);
    cells_.beam.fieldAngle = degToRad(40.0f);
    cells_.beam.luminousFlux = 300.0f;
    cells_.beam.colorTemperature = 5600.0f;
    cells_.beam.emitterSize = glm::vec2(cells_.pitch * 0.9f, 0.06f);
    cellDirection_ = 0;
    cellChannels_ = 1;
    cellWhite_ = false;
    cellsPopupRequested_ = true;
}

bool GeometrySection::drawCellsPopup(fixtures::FixtureType& type, int modeIndex) {
    if (cellsPopupRequested_) {
        ImGui::OpenPopup("Make cells");
        cellsPopupRequested_ = false;
    }
    bool changed = false;
    if (!ImGui::BeginPopupModal("Make cells", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return false;

    const Geometry* parent = nodeAt(type.geometry, selected_);
    if (parent == nullptr) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return false;
    }
    ImGui::Text("Creates a row of beam cells below \"%s\".", parent->name.c_str());

    ImGui::InputInt("Cells", &cells_.count);
    cells_.count = std::clamp(cells_.count, 1, 128);
    ImGui::DragFloat("Pitch", &cells_.pitch, 0.001f, 0.001f, 5.0f, "%.3f m");
    ImGui::SetItemTooltip("Distance between the centres of two cells.");
    if (comboIndex("Row direction", cellDirection_, kDirectionNames)) {
        cells_.axis = glm::vec3(cellDirection_ == 0 ? 1.0f : 0.0f, cellDirection_ == 1 ? 1.0f : 0.0f, cellDirection_ == 2 ? 1.0f : 0.0f);
    }
    dragVec3("Row centre", cells_.centre, 0.001f, -20.0f, 20.0f, "%.3f m");
    ImGui::SetItemTooltip("Centre of the row, relative to the selected node. Cells shine along -Y.");
    inputText("Name prefix", cells_.namePrefix);
    int shape = static_cast<int>(cells_.beam.type);
    if (comboIndex("Cell shape", shape, kBeamNames)) cells_.beam.type = static_cast<fixtures::BeamType>(shape);
    ImGui::DragFloat("Flux per cell", &cells_.beam.luminousFlux, 5.0f, 0.0f, 50000.0f, "%.0f lm");
    ImGui::DragFloat2("Cell size", &cells_.beam.emitterSize.x, 0.001f, 0.001f, 2.0f, "%.3f m");
    ImGui::SetItemTooltip("Width and height of a rectangular cell.");

    const bool haveMode = modeIndex >= 0 && modeIndex < static_cast<int>(type.modes.size());
    ImGui::BeginDisabled(!haveMode);
    comboIndex("Channels", cellChannels_, kCellChannelNames);
    ImGui::Checkbox("Include white", &cellWhite_);
    ImGui::EndDisabled();
    if (haveMode)
        ImGui::TextDisabled("Channels go into mode \"%s\".", type.modes[static_cast<std::size_t>(modeIndex)].name.c_str());

    ImGui::Separator();
    if (ImGui::Button("Create", ImVec2(110.0f, 0.0f))) {
        const std::vector<std::string> names = makeCells(type, selected_, cells_);
        if (!names.empty()) {
            changed = true;
            if (haveMode && cellChannels_ > 0) {
                std::vector<Attribute> attributes = {Attribute::ColorAdd_R, Attribute::ColorAdd_G, Attribute::ColorAdd_B};
                if (cellWhite_) attributes.push_back(Attribute::ColorAdd_W);
                addCellChannels(type, type.modes[static_cast<std::size_t>(modeIndex)], names, attributes, cellChannels_ == 2,
                                "All " + cells_.namePrefix + "s");
            }
            if (const auto first = findNodePath(type.geometry, names.front())) selected_ = *first;
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return changed;
}

// ---------------------------------------------------------------------------
// Mesh import

void GeometrySection::pollModelImport(fixtures::FixtureType& type, bool& changed) {
    const std::optional<FileDialogs::Result> result = dialogs_.poll();
    if (!result || result->path.empty()) return;
    lastImportDir_ = result->path.parent_path();

    std::string error;
    std::optional<ImportedFile> file = readModelFile(result->path, &error);
    Geometry* node = nodeAt(type.geometry, importTarget_);
    if (!file || node == nullptr) {
        message_ = file ? "The node no longer exists." : "Cannot use " + result->path.filename().string() + ": " + error;
        return;
    }
    message_.clear();
    node->model.mesh = addResource(type, file->name, file->format, std::move(file->data));
    removeUnusedResources(type);  // the mesh this one replaced
    changed = true;
}

}  // namespace dmxviz::ui::fixture_editor
