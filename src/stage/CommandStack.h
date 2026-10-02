#pragma once
// Undo/redo history of scene edits.

#include "stage/Command.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dmxviz::stage {

class Scene;
class CompoundCommand;

// Executes commands on one Scene and keeps the undo and redo lists.
//
// Typical UI use:
//   stack.execute(std::make_unique<SetTransformCommand>(id, t));   // every drag step
//   stack.breakMerge();                                            // on mouse release
//   if (stack.canUndo()) stack.undo();
//
// Merging: a command whose mergeWith() accepts the next command absorbs it,
// until breakMerge(), undo() or redo() is called.
// Batches: commands executed between beginBatch() and endBatch() form one
// undo step (e.g. a tool that creates many nodes through several commands).
// Dirty flag: isDirty() is true when the scene differs from the last
// markSaved() state, also after undoing past it.
class CommandStack {
public:
    explicit CommandStack(Scene& scene, std::size_t limit = 500);
    ~CommandStack();
    CommandStack(const CommandStack&) = delete;
    CommandStack& operator=(const CommandStack&) = delete;

    // Applies the command and records it. Returns the command that now holds
    // the edit (the absorbing command when merged), or nullptr when apply()
    // failed (nothing is recorded then).
    Command* execute(std::unique_ptr<Command> command);

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    bool undo();
    bool redo();
    std::string undoName() const;
    std::string redoName() const;
    std::size_t undoCount() const { return undo_.size(); }
    std::size_t redoCount() const { return redo_.size(); }

    // Forgets all history (e.g. after loading a project). Keeps the dirty state.
    void clear();
    // The next command never merges into the current top.
    void breakMerge() { mergeOpen_ = false; }

    void beginBatch(std::string name);
    void endBatch();
    bool inBatch() const { return batchDepth_ > 0; }

    void markSaved();
    bool isDirty() const;

    // Incremented by execute/undo/redo/clear (for menus that cache labels).
    std::uint64_t revision() const { return revision_; }
    Scene& scene() { return scene_; }

private:
    struct Entry {
        std::unique_ptr<Command> command;
        std::uint64_t state = 0;  // identifies the scene state after this command
    };

    std::uint64_t currentState() const;
    void trim();

    Scene& scene_;
    std::size_t limit_;
    std::vector<Entry> undo_;
    std::vector<Entry> redo_;
    bool mergeOpen_ = false;
    std::uint64_t nextState_ = 1;
    std::uint64_t baseState_ = 0;   // state when the undo list is empty
    std::uint64_t savedState_ = 0;
    std::uint64_t revision_ = 0;
    std::unique_ptr<CompoundCommand> batch_;
    int batchDepth_ = 0;
};

}  // namespace dmxviz::stage
