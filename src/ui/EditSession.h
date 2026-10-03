#pragma once
// EditSession: turns the widget changes of one frame into one undoable command.
//
// Inspector-style panels copy the data of a node, let ImGui widgets edit the
// copy, and then run a single EditNodesCommand with the result. Wrap every
// widget in touch():
//
//   stage::NodeData data = node->data();
//   EditSession edit(ctx);
//   edit.touch(ImGui::DragFloat("Height", &deck.height), "Height");
//   ...
//   if (edit.changed()) ctx.commands.execute(edit.makeCommand({{id, data}}));
//   edit.finish();
//
// Dragging a slider produces a command every frame; they all carry the same
// merge key, so the command stack folds them into one undo step. A new gesture
// (pressing the widget) always starts a new step.

#include "stage/Commands.h"
#include "ui/EditorContext.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace dmxviz::ui {

class EditSession {
public:
    explicit EditSession(EditorContext& ctx) : ctx_(ctx) {}

    // Call directly after a widget with the widget's return value; `label`
    // names the edit in the undo menu and identifies the merge key. Returns `widgetChanged`.
    bool touch(bool widgetChanged, const char* label);

    bool changed() const { return changed_; }
    const std::string& label() const { return label_; }

    // One command that sets the given node data; it merges with the previous
    // command of the same widget while a drag is in progress.
    std::unique_ptr<stage::EditNodesCommand> makeCommand(std::vector<std::pair<NodeId, stage::NodeData>> after) const;

    // Ends the merge window when the gesture is finished. Call after executing the command.
    void finish();

private:
    EditorContext& ctx_;
    bool changed_ = false;
    bool gestureEnded_ = false;
    std::string label_;
};

}  // namespace dmxviz::ui
