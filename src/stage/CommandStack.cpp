#include "stage/CommandStack.h"

#include "stage/Commands.h"

#include <limits>

namespace dmxviz::stage {
namespace {
constexpr std::uint64_t kNeverSaved = std::numeric_limits<std::uint64_t>::max();
}

CommandStack::CommandStack(Scene& scene, std::size_t limit) : scene_(scene), limit_(limit < 1 ? 1 : limit) {}

CommandStack::~CommandStack() = default;

Command* CommandStack::execute(std::unique_ptr<Command> command) {
    if (!command || !command->apply(scene_)) return nullptr;
    ++revision_;
    redo_.clear();
    if (batchDepth_ > 0) {
        Command* raw = command.get();
        batch_->addApplied(std::move(command));
        return raw;
    }
    if (mergeOpen_ && !undo_.empty() && undo_.back().command->mergeWith(*command)) {
        undo_.back().state = nextState_++;  // same undo step, different scene state
        return undo_.back().command.get();
    }
    undo_.push_back({std::move(command), nextState_++});
    mergeOpen_ = true;
    trim();
    return undo_.back().command.get();
}

bool CommandStack::undo() {
    if (undo_.empty() || inBatch()) return false;
    Entry e = std::move(undo_.back());
    undo_.pop_back();
    e.command->revert(scene_);
    redo_.push_back(std::move(e));
    mergeOpen_ = false;
    ++revision_;
    return true;
}

bool CommandStack::redo() {
    if (redo_.empty() || inBatch()) return false;
    Entry e = std::move(redo_.back());
    redo_.pop_back();
    mergeOpen_ = false;
    ++revision_;
    if (!e.command->apply(scene_)) {
        // The scene no longer allows this edit; the rest of the redo list
        // depends on it, so drop it all.
        redo_.clear();
        return false;
    }
    undo_.push_back(std::move(e));
    return true;
}

std::string CommandStack::undoName() const { return undo_.empty() ? std::string() : undo_.back().command->name(); }

std::string CommandStack::redoName() const { return redo_.empty() ? std::string() : redo_.back().command->name(); }

void CommandStack::clear() {
    const bool dirty = isDirty();
    undo_.clear();
    redo_.clear();
    batch_.reset();
    batchDepth_ = 0;
    mergeOpen_ = false;
    baseState_ = nextState_++;
    savedState_ = dirty ? kNeverSaved : baseState_;
    ++revision_;
}

void CommandStack::beginBatch(std::string name) {
    if (batchDepth_++ == 0) batch_ = std::make_unique<CompoundCommand>(std::move(name));
}

void CommandStack::endBatch() {
    if (batchDepth_ == 0 || --batchDepth_ > 0) return;
    std::unique_ptr<CompoundCommand> batch = std::move(batch_);
    if (!batch || batch->empty()) return;
    undo_.push_back({std::move(batch), nextState_++});
    mergeOpen_ = false;
    trim();
    ++revision_;
}

void CommandStack::markSaved() { savedState_ = currentState(); }

bool CommandStack::isDirty() const {
    if (batch_ && !batch_->empty()) return true;
    return currentState() != savedState_;
}

std::uint64_t CommandStack::currentState() const { return undo_.empty() ? baseState_ : undo_.back().state; }

void CommandStack::trim() {
    while (undo_.size() > limit_) {
        // The state after the oldest command becomes the new "nothing to undo" state.
        baseState_ = undo_.front().state;
        undo_.erase(undo_.begin());
    }
}

}  // namespace dmxviz::stage
