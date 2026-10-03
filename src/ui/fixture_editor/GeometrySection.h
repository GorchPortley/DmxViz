#pragma once
// GeometrySection: the "Geometry" tab of the fixture editor.
//
// Left: the geometry tree with buttons to add Generic / Axis / Beam children, reorder and remove
// nodes, and "Make cells" (a row of beam cells for pixel bars). Right: the properties of the
// selected node: name, type, position and rotation, model (built-in shape with size, or a mesh
// file stored in the fixture), the Pan/Tilt channels that drive an Axis, and the optics of a Beam.
// Edits that change the structure are executed after the tree was drawn, never while iterating it.

#include "ui/FileDialogs.h"
#include "ui/fixture_editor/EditDocument.h"
#include "ui/fixture_editor/GeometryEditing.h"

#include <string>

namespace dmxviz::ui::fixture_editor {

class GeometrySection {
public:
    // `modeIndex` is the DMX mode that new Pan/Tilt channels and cell channels are added to.
    // Returns true when the fixture type changed.
    bool draw(EditDocument& doc, int modeIndex);

    // Selects a node by name (jump from the validation list). Unknown names are ignored.
    void select(const fixtures::FixtureType& type, const std::string& geometryName);
    // Forget the selection (another fixture was opened).
    void reset();

private:
    enum class Action { None, AddGeneric, AddAxis, AddBeam, Remove, MoveUp, MoveDown, MakeCells };

    void drawToolbar(const fixtures::FixtureType& type);
    void drawTreeNode(const fixtures::Geometry& node, NodePath& path);
    bool drawProperties(EditDocument& doc, int modeIndex);
    bool drawName(fixtures::FixtureType& type, fixtures::Geometry& node);
    bool drawTransform(fixtures::Geometry& node);
    bool drawModel(fixtures::FixtureType& type, fixtures::Geometry& node);
    bool drawAxis(fixtures::FixtureType& type, const fixtures::Geometry& node, int modeIndex);
    bool drawBeam(fixtures::Geometry& node);
    bool drawCellsPopup(fixtures::FixtureType& type, int modeIndex);

    bool runAction(fixtures::FixtureType& type, int modeIndex);
    void openCellsPopup(const fixtures::FixtureType& type);
    void pollModelImport(fixtures::FixtureType& type, bool& changed);

    NodePath selected_;
    Action pending_ = Action::None;

    // Name field: edited as text, applied when the user leaves the field.
    std::string nameEdit_;
    std::string nameEditSource_;
    NodePath nameEditPath_;

    // The rotation is stored as a quaternion but edited as Euler angles; keeping the angles between
    // frames stops them from jumping to an equivalent set while dragging.
    glm::vec3 eulerDegrees_{0.0f};
    glm::quat eulerSource_{1.0f, 0.0f, 0.0f, 0.0f};
    NodePath eulerPath_;

    // "Make cells" dialog.
    CellOptions cells_;
    int cellDirection_ = 0;      // 0 = X, 1 = Y, 2 = Z
    int cellChannels_ = 1;       // 0 none, 1 one shared RGB set, 2 RGB per cell
    bool cellWhite_ = false;
    bool cellsPopupRequested_ = false;

    FileDialogs dialogs_;
    NodePath importTarget_;
    std::filesystem::path lastImportDir_;
    std::string message_;
};

}  // namespace dmxviz::ui::fixture_editor
