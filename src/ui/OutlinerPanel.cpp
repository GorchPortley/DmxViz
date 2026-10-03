#include "ui/OutlinerPanel.h"

#include "stage/NodeFactory.h"
#include "ui/EditorContext.h"
#include "ui/NodeActions.h"
#include "ui/NodeIcons.h"
#include "ui/PanelTitles.h"

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <string>

namespace dmxviz::ui {

using namespace stage;

namespace {

constexpr const char* kDragPayload = "DMXVIZ_NODES";
constexpr const char* kContextPopup = "##outlinerContext";

bool contains(const std::vector<NodeId>& ids, NodeId id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

ImU32 withAlpha(ImU32 color, float alpha) {
    const float a = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) * alpha;
    return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
}

// Small grey text at the right of a row, e.g. the DMX address of a fixture.
std::string rowSuffix(const Node& node) {
    if (const FixtureContent* fixture = node.as<FixtureContent>()) {
        if (!fixture->patch.patched()) return "unpatched";
        char text[32];
        std::snprintf(text, sizeof(text), "%u.%03u", fixture->patch.universe, fixture->patch.address);
        return text;
    }
    return {};
}

}  // namespace

const char* OutlinerPanel::title() const {
    return kOutlinerTitle;
}

// ---------------------------------------------------------------------------
// Frame

void OutlinerPanel::draw(EditorContext& ctx) {
    drawFilterBar();
    handleKeys(ctx);
    revealSelection(ctx);
    buildRows(ctx);
    hoveredRow_ = kInvalidNode;

    ImGui::BeginChild("##outlinerRows", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    const float rowStride = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows_.size()), rowStride);
    if (scrollTo_ != kInvalidNode) {
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].id == scrollTo_) clipper.IncludeItemByIndex(static_cast<int>(i));
    }
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            drawRow(ctx, rows_[static_cast<std::size_t>(i)], static_cast<std::size_t>(i));
    }
    clipper.End();
    scrollTo_ = kInvalidNode;

    if (rows_.empty())
        ImGui::TextDisabled(filter_.IsActive() ? "No node matches the filter."
                                               : "The scene is empty. Use the Add menu.");
    drawTail(ctx, ImGui::GetFrameHeight());
    drawContextMenu(ctx);
    ImGui::EndChild();

    // Let the viewport highlight the node under the mouse, like it does for its own hover.
    if (hoveredRow_ != kInvalidNode) {
        if (ctx.selection.hover() != hoveredRow_) ctx.selection.setHover(hoveredRow_);
        hoverOwned_ = true;
    } else if (hoverOwned_) {
        ctx.selection.setHover(kInvalidNode);
        hoverOwned_ = false;
    }
}

void OutlinerPanel::drawFilterBar() {
    const float clearWidth = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(-clearWidth - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::InputTextWithHint("##outlinerFilter", "Filter by name...", filter_.InputBuf,
                                 IM_ARRAYSIZE(filter_.InputBuf)))
        filter_.Build();
    ImGui::SameLine();
    ImGui::BeginDisabled(!filter_.IsActive());
    if (ImGui::Button("x", ImVec2(clearWidth, 0.0f))) filter_.Clear();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Clear the filter");
}

// ---------------------------------------------------------------------------
// Rows

void OutlinerPanel::buildRows(const EditorContext& ctx) {
    rows_.clear();
    bool renameRowExists = false;
    for (NodeId root : ctx.scene.roots()) addRows(ctx, root, 0);
    for (const Row& row : rows_) renameRowExists = renameRowExists || row.id == renaming_;
    if (!renameRowExists) renaming_ = kInvalidNode;
}

// Appends the row of `id` and, when expanded, its descendants. With an active
// filter only matching nodes and their parents stay. Returns false when nothing was added.
bool OutlinerPanel::addRows(const EditorContext& ctx, NodeId id, int depth) {
    const Node* node = ctx.scene.find(id);
    if (node == nullptr) return false;

    const bool filtering = filter_.IsActive();
    const bool expanded = filtering || collapsed_.count(id) == 0;
    const std::size_t rowIndex = rows_.size();
    rows_.push_back(Row{id, depth, !node->children().empty(), expanded});

    bool childAdded = false;
    if (expanded) {
        for (NodeId child : node->children()) childAdded = addRows(ctx, child, depth + 1) || childAdded;
    }
    if (filtering && !childAdded && !filter_.PassFilter(node->name().c_str())) {
        rows_.resize(rowIndex);
        return false;
    }
    return true;
}

void OutlinerPanel::revealSelection(EditorContext& ctx) {
    const NodeId primary = ctx.selection.primary();
    if (primary == lastPrimary_) return;
    lastPrimary_ = primary;
    const Node* node = ctx.scene.find(primary);
    if (node == nullptr) return;
    // Selected in the viewport: open the parents so the row exists, and scroll to it if needed.
    for (NodeId parent = node->parent(); parent != kInvalidNode; parent = ctx.scene.find(parent)->parent())
        collapsed_.erase(parent);
    scrollTo_ = primary;
}

void OutlinerPanel::drawRow(EditorContext& ctx, const Row& row, std::size_t index) {
    const Node* node = ctx.scene.find(row.id);
    if (node == nullptr) return;

    ImGui::PushID(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(row.id)));
    const float h = ImGui::GetFrameHeight();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const bool selected = ctx.selection.contains(row.id);

    // The whole row is one selectable; arrow and toggles are small buttons on top of it.
    ImGui::SetNextItemAllowOverlap();
    const bool pressed = ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, h));
    const bool rowHovered = ImGui::IsItemHovered();
    const bool visible = ImGui::IsItemVisible();
    const ImVec2 afterRow = ImGui::GetCursorScreenPos();
    if (pressed) {
        handleClick(ctx, index);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) startRename(ctx, row.id);
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        if (!selected) ctx.selection.set(row.id);
        openContextMenu_ = true;
        contextMenuOnEmpty_ = false;
    }
    handleDragDrop(ctx, row, min, h, width);
    if (rowHovered) hoveredRow_ = row.id;
    if (row.id == scrollTo_ && !visible) ImGui::SetScrollHereY(0.5f);

    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 dimColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const bool effectivelyVisible = ctx.scene.effectiveVisible(row.id);

    // Expand / collapse arrow.
    float x = min.x + static_cast<float>(row.depth) * h * 0.9f + 2.0f;
    if (row.hasChildren && !filter_.IsActive()) {
        ImGui::SetCursorScreenPos(ImVec2(x, min.y));
        if (ImGui::InvisibleButton("##arrow", ImVec2(h, h))) {
            if (row.expanded)
                collapsed_.insert(row.id);
            else
                collapsed_.erase(row.id);
        }
        const ImVec2 c(x + h * 0.5f, min.y + h * 0.5f);
        const float r = h * 0.2f;
        if (row.expanded)
            list->AddTriangleFilled(ImVec2(c.x - r, c.y - r * 0.6f), ImVec2(c.x + r, c.y - r * 0.6f),
                                    ImVec2(c.x, c.y + r), dimColor);
        else
            list->AddTriangleFilled(ImVec2(c.x - r * 0.6f, c.y - r), ImVec2(c.x - r * 0.6f, c.y + r),
                                    ImVec2(c.x + r, c.y), dimColor);
    }
    x += h;

    // Kind icon and name.
    const float iconSize = h * 0.7f;
    drawNodeKindIcon(list, ImVec2(x, min.y + (h - iconSize) * 0.5f), iconSize, node->kind(),
                     effectivelyVisible ? nodeKindColor(node->kind()) : withAlpha(nodeKindColor(node->kind()), 0.4f));
    x += iconSize + 6.0f;

    const float togglesX = min.x + width - 2.0f * h;
    if (renaming_ == row.id) {
        drawRename(ctx, ImVec2(x, min.y), togglesX - x - 4.0f);
    } else {
        const std::string suffix = rowSuffix(*node);
        const float suffixWidth = suffix.empty() ? 0.0f : ImGui::CalcTextSize(suffix.c_str()).x + 8.0f;
        const std::string& name = node->name();
        const ImVec2 textPos(x, min.y + (h - ImGui::GetTextLineHeight()) * 0.5f);
        list->PushClipRect(ImVec2(x, min.y), ImVec2(togglesX - suffixWidth - 2.0f, min.y + h), true);
        list->AddText(textPos, effectivelyVisible ? textColor : dimColor, name.empty() ? "(unnamed)" : name.c_str());
        list->PopClipRect();
        if (!suffix.empty()) list->AddText(ImVec2(togglesX - suffixWidth, textPos.y), dimColor, suffix.c_str());
    }

    // Visibility and lock toggles.
    const bool ownVisible = node->data().visible;
    ImGui::SetCursorScreenPos(ImVec2(togglesX, min.y));
    if (ImGui::InvisibleButton("##visible", ImVec2(h, h))) actions::setVisible(ctx, {row.id}, !ownVisible);
    ImGui::SetItemTooltip(ownVisible ? "Hide" : "Show");
    drawEyeIcon(list, ImVec2(togglesX + h * 0.15f, min.y + h * 0.15f), h * 0.7f, ownVisible,
                !ownVisible ? withAlpha(textColor, 0.5f) : (effectivelyVisible ? textColor : dimColor));

    const bool ownLocked = node->data().locked;
    ImGui::SetCursorScreenPos(ImVec2(togglesX + h, min.y));
    if (ImGui::InvisibleButton("##locked", ImVec2(h, h))) actions::setLocked(ctx, {row.id}, !ownLocked);
    ImGui::SetItemTooltip(ownLocked ? "Unlock" : "Lock");
    const bool lockHot = ImGui::IsItemHovered() || rowHovered;
    if (ownLocked || lockHot)
        drawLockIcon(list, ImVec2(togglesX + h + h * 0.15f, min.y + h * 0.15f), h * 0.7f, ownLocked,
                     ownLocked ? IM_COL32(240, 170, 80, 255) : withAlpha(textColor, 0.35f));

    ImGui::SetCursorScreenPos(afterRow);
    ImGui::PopID();
}

void OutlinerPanel::drawRename(EditorContext& ctx, const ImVec2& pos, float width) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::SetNextItemWidth(std::max(width, 40.0f));
    if (renameNeedsFocus_) {
        ImGui::SetKeyboardFocusHere();
        renameNeedsFocus_ = false;
    }
    const bool enter = ImGui::InputText("##rename", renameBuffer_, sizeof(renameBuffer_),
                                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
    if (enter || (ImGui::IsItemDeactivated() && !cancelled)) actions::renameNode(ctx, renaming_, renameBuffer_);
    if (enter || ImGui::IsItemDeactivated()) renaming_ = kInvalidNode;
}

void OutlinerPanel::startRename(const EditorContext& ctx, NodeId id) {
    const Node* node = ctx.scene.find(id);
    if (node == nullptr) return;
    renaming_ = id;
    renameNeedsFocus_ = true;
    std::snprintf(renameBuffer_, sizeof(renameBuffer_), "%s", node->name().c_str());
}

// The empty area below the rows: click to deselect, drop to move nodes to the top level.
void OutlinerPanel::drawTail(EditorContext& ctx, float minHeight) {
    const float height = std::max(ImGui::GetContentRegionAvail().y, minHeight);
    ImGui::InvisibleButton("##outlinerTail", ImVec2(-FLT_MIN, height));
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !io.KeyCtrl && !io.KeyShift) ctx.selection.clear();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        openContextMenu_ = true;
        contextMenuOnEmpty_ = true;
    }
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kDragPayload);
        if (payload != nullptr) actions::reparentNodes(ctx, dragged_, kInvalidNode, -1);
        ImGui::EndDragDropTarget();
    }
}

// ---------------------------------------------------------------------------
// Selection

void OutlinerPanel::handleClick(EditorContext& ctx, std::size_t rowIndex) {
    const ImGuiIO& io = ImGui::GetIO();
    const NodeId id = rows_[rowIndex].id;

    if (io.KeyShift && anchor_ != kInvalidNode) {
        std::size_t anchorIndex = rowIndex;
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].id == anchor_) anchorIndex = i;
        const std::size_t from = std::min(anchorIndex, rowIndex);
        const std::size_t to = std::max(anchorIndex, rowIndex);
        std::vector<NodeId> range;
        if (io.KeyCtrl) range = ctx.selection.ids();  // Ctrl+Shift adds the range to the selection
        for (std::size_t i = from; i <= to; ++i)
            if (!contains(range, rows_[i].id)) range.push_back(rows_[i].id);
        ctx.selection.setMany(range);
        ctx.selection.setPrimary(id);
    } else if (io.KeyCtrl) {
        ctx.selection.toggle(id);
        anchor_ = id;
    } else {
        ctx.selection.set(id);
        anchor_ = id;
    }
}

// ---------------------------------------------------------------------------
// Drag and drop

void OutlinerPanel::handleDragDrop(EditorContext& ctx, const Row& row, const ImVec2& rowMin, float rowHeight,
                                   float rowWidth) {
    if (!ctx.scene.effectiveLocked(row.id) && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        if (!ctx.selection.contains(row.id)) ctx.selection.set(row.id);
        dragged_ = actions::editableTopLevel(ctx, ctx.selection.ids());
        const NodeId id = row.id;
        ImGui::SetDragDropPayload(kDragPayload, &id, sizeof(id));
        if (dragged_.size() > 1)
            ImGui::Text("%zu nodes", dragged_.size());
        else
            ImGui::TextUnformatted(ctx.scene.find(row.id)->name().c_str());
        ImGui::EndDragDropSource();
    }

    if (!ImGui::BeginDragDropTarget()) return;
    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
        kDragPayload, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
    if (payload != nullptr) {
        // Upper quarter = before, lower quarter = after, the middle = become a child.
        const float t = (ImGui::GetMousePos().y - rowMin.y) / rowHeight;
        const DropZone zone = t < 0.25f ? DropZone::Before : (t > 0.75f ? DropZone::After : DropZone::Into);
        const Node* target = ctx.scene.find(row.id);
        const NodeId newParent = zone == DropZone::Into ? row.id : target->parent();
        const bool valid = canDropOn(ctx, newParent) && !(zone != DropZone::Into && contains(dragged_, row.id));
        if (valid) {
            ImDrawList* list = ImGui::GetWindowDrawList();
            const ImU32 color = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
            const ImVec2 max(rowMin.x + rowWidth, rowMin.y + rowHeight);
            if (zone == DropZone::Into)
                list->AddRect(rowMin, max, color, 0.0f, 0, 2.0f);
            else
                list->AddLine(ImVec2(rowMin.x, zone == DropZone::Before ? rowMin.y : max.y),
                              ImVec2(max.x, zone == DropZone::Before ? rowMin.y : max.y), color, 2.0f);
            if (payload->IsDelivery()) dropOnRow(ctx, row.id, zone);
        }
    }
    ImGui::EndDragDropTarget();
}

bool OutlinerPanel::canDropOn(const EditorContext& ctx, NodeId newParent) const {
    if (dragged_.empty()) return false;
    for (NodeId id : dragged_) {
        if (id == newParent || ctx.scene.isAncestor(id, newParent)) return false;  // would create a cycle
    }
    return true;
}

void OutlinerPanel::dropOnRow(EditorContext& ctx, NodeId target, DropZone zone) {
    const Node* node = ctx.scene.find(target);
    if (node == nullptr) return;
    if (zone == DropZone::Into) {
        collapsed_.erase(target);  // show where the nodes went
        actions::reparentNodes(ctx, dragged_, target, -1);
        return;
    }
    // Position among the siblings once the dragged nodes have been taken out of the list.
    const NodeId parent = node->parent();
    int index = 0;
    for (NodeId sibling : ctx.scene.childrenOf(parent)) {
        if (sibling == target) break;
        if (!contains(dragged_, sibling)) ++index;
    }
    actions::reparentNodes(ctx, dragged_, parent, zone == DropZone::After ? index + 1 : index);
}

// ---------------------------------------------------------------------------
// Keyboard and context menu

void OutlinerPanel::handleKeys(EditorContext& ctx) {
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || ctx.selection.empty()) return;

    const std::vector<NodeId> ids = ctx.selection.ids();  // commands change the selection
    if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) startRename(ctx, ctx.selection.primary());
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) actions::deleteNodes(ctx, ids);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) actions::duplicateNodes(ctx, ids);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G, false)) {
        if (io.KeyShift)
            actions::ungroupNodes(ctx, ids);
        else
            actions::groupNodes(ctx, ids);
    }
    if (!io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_F, false)) actions::frameNodes(ctx, ids);
}

void OutlinerPanel::drawContextMenu(EditorContext& ctx) {
    if (openContextMenu_) {
        ImGui::OpenPopup(kContextPopup);
        openContextMenu_ = false;
    }
    if (!ImGui::BeginPopup(kContextPopup)) return;

    if (contextMenuOnEmpty_) {
        if (ImGui::MenuItem("New group")) actions::addNodeAtPlacement(ctx, factory::group());
        ImGui::EndPopup();
        return;
    }

    const std::vector<NodeId> ids = ctx.selection.ids();  // the commands below change the selection
    const bool any = !ids.empty();
    bool anyGroup = false;
    for (NodeId id : ids) {
        const Node* node = ctx.scene.find(id);
        anyGroup = anyGroup || (node != nullptr && node->kind() == NodeKind::Group);
    }

    if (ImGui::MenuItem("Rename", "F2", false, any)) startRename(ctx, ctx.selection.primary());
    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, any)) actions::duplicateNodes(ctx, ids);
    if (ImGui::MenuItem("Delete", "Del", false, any)) actions::deleteNodes(ctx, ids);
    ImGui::Separator();
    if (ImGui::MenuItem("Group", "Ctrl+G", false, any)) actions::groupNodes(ctx, ids);
    if (ImGui::MenuItem("Ungroup", "Ctrl+Shift+G", false, anyGroup)) actions::ungroupNodes(ctx, ids);
    ImGui::Separator();
    if (ImGui::MenuItem("Frame", "F", false, any)) actions::frameNodes(ctx, ids);
    if (ImGui::MenuItem("Show", nullptr, false, any)) actions::setVisible(ctx, ids, true);
    if (ImGui::MenuItem("Hide", nullptr, false, any)) actions::setVisible(ctx, ids, false);
    if (ImGui::MenuItem("Lock", nullptr, false, any)) actions::setLocked(ctx, ids, true);
    if (ImGui::MenuItem("Unlock", nullptr, false, any)) actions::setLocked(ctx, ids, false);
    ImGui::EndPopup();
}

}  // namespace dmxviz::ui
