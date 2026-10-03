#pragma once
// InspectorPanel: edits the primary selected node.
//
//   * one node selected: name, visibility/lock/layer, transform (position,
//     Euler rotation, scale), the editor of its kind (primitive, truss, deck,
//     fixture patch...) and an optional material override;
//   * several nodes: a summary with batch actions (show/hide, lock, move by,
//     colour).
//
// Every change becomes a command (see EditSession.h), so it can be undone, and a
// slider drag is a single undo step.

#include "stage/Node.h"
#include "ui/FileDialogs.h"
#include "ui/Panel.h"

namespace dmxviz::ui {

class EditSession;

class InspectorPanel : public Panel {
public:
    const char* title() const override;
    void draw(EditorContext& ctx) override;

private:
    void drawSingle(EditorContext& ctx, const stage::Node& node);
    void drawMultiple(EditorContext& ctx);

    void drawHeader(EditorContext& ctx, const stage::Node& node, EditSession& edit, stage::NodeData& data);
    void drawTransform(EditorContext& ctx, const stage::Node& node);
    void drawContent(EditorContext& ctx, const stage::Node& node, EditSession& edit, stage::NodeData& data);
    void drawModelFile(EditorContext& ctx, const stage::Node& node, EditSession& edit, stage::NodeData& data);
    void drawCameraPreset(EditorContext& ctx, const stage::Node& node, EditSession& edit, stage::NodeData& data);
    void drawMaterialOverride(EditSession& edit, stage::NodeData& data);

    // Rotation is stored as a quaternion; remembering the Euler angles the user typed
    // keeps the fields from jumping to an equivalent set of angles while dragging.
    glm::vec3 eulerFor(const stage::Node& node);

    void pollModelDialog(EditorContext& ctx);

    FileDialogs dialogs_;
    NodeId modelDialogNode_ = kInvalidNode;  // the node a running "Browse..." dialog is for
    NodeId eulerNode_ = kInvalidNode;
    glm::vec3 euler_{0.0f};
    glm::quat eulerQuat_{1.0f, 0.0f, 0.0f, 0.0f};
    bool uniformScale_ = true;
};

}  // namespace dmxviz::ui
