#pragma once
// OutlinerPanel: the scene tree.
//
//   click / Ctrl+click / Shift+click ... select (shared with the viewport through Selection)
//   double-click or F2 ................. rename
//   drag and drop ...................... reparent: drop on a row to make a child, on its upper or lower edge to
//                                        place the node before or after it
//   eye / lock icons ................... visibility and lock of one node
//   right-click ........................ context menu (rename, duplicate, delete, group, ungroup, frame...)
//   Delete, Ctrl+D, Ctrl+G, Ctrl+Shift+G, F ... shortcuts while the outliner has focus
//   filter box ......................... shows only nodes whose name contains the text (and their parents)
//
// The tree is drawn as a flat list of rows (see Row) so that thousands of nodes stay cheap
// (ImGuiListClipper) and shift-click can select a range. Every edit goes through the CommandStack.

#include "stage/Node.h"
#include "ui/Panel.h"

#include "imgui.h"

#include <unordered_set>
#include <vector>

namespace dmxviz::ui {

class OutlinerPanel : public Panel {
public:
    const char* title() const override;
    void draw(EditorContext& ctx) override;

private:
    // One visible line of the tree.
    struct Row {
        NodeId id = kInvalidNode;
        int depth = 0;
        bool hasChildren = false;
        bool expanded = false;
    };
    // Where on a row something was dropped.
    enum class DropZone { Before, Into, After };

    void drawFilterBar();
    void buildRows(const EditorContext& ctx);
    bool addRows(const EditorContext& ctx, NodeId id, int depth);
    void revealSelection(EditorContext& ctx);
    void handleKeys(EditorContext& ctx);

    void drawRow(EditorContext& ctx, const Row& row, std::size_t index);
    void drawRename(EditorContext& ctx, const ImVec2& pos, float width);
    void drawTail(EditorContext& ctx, float minHeight);
    void drawContextMenu(EditorContext& ctx);

    void handleClick(EditorContext& ctx, std::size_t rowIndex);
    void handleDragDrop(EditorContext& ctx, const Row& row, const ImVec2& rowMin, float rowHeight, float rowWidth);
    void dropOnRow(EditorContext& ctx, NodeId target, DropZone zone);
    bool canDropOn(const EditorContext& ctx, NodeId newParent) const;
    void startRename(const EditorContext& ctx, NodeId id);

    std::vector<Row> rows_;
    std::unordered_set<NodeId> collapsed_;  // new nodes start expanded
    ImGuiTextFilter filter_;

    NodeId renaming_ = kInvalidNode;
    char renameBuffer_[256] = {};
    bool renameNeedsFocus_ = false;

    NodeId anchor_ = kInvalidNode;       // start of a Shift+click range
    NodeId lastPrimary_ = kInvalidNode;  // to notice selection changes made elsewhere
    NodeId scrollTo_ = kInvalidNode;
    NodeId hoveredRow_ = kInvalidNode;
    bool hoverOwned_ = false;
    bool openContextMenu_ = false;  // a row (or the empty area) was right-clicked this frame
    bool contextMenuOnEmpty_ = false;
    std::vector<NodeId> dragged_;  // top-level nodes of the drag in progress
};

}  // namespace dmxviz::ui
