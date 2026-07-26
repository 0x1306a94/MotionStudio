#include "MotionStudio/undo/UndoManager.h"

#include <utility>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/undo/CompositeCommand.h"

namespace motion {

UndoManager::UndoManager(size_t maxHistory, std::chrono::milliseconds mergeWindow)
    : maxHistory_(maxHistory)
    , mergeWindow_(mergeWindow) {
}

void UndoManager::absorbIntoComposite(std::unique_ptr<Command> command) {
    auto &top = undoStack_.back();
    if (CompositeCommand *composite = top->asComposite()) {
        composite->add(std::move(command));
        return;
    }
    auto composite = std::make_unique<CompositeCommand>(top->describe());
    composite->add(std::move(top));
    composite->add(std::move(command));
    undoStack_.back() = std::move(composite);
}

void UndoManager::execute(Document &document, std::unique_ptr<Command> command) {
    command->execute(document);

    const auto now = std::chrono::steady_clock::now();

    // Explicit drag transaction: first command starts a fresh undo unit; later
    // mixed-property commands pack into that unit (or merge by property).
    if (packMergesIntoComposite_) {
        if (!mergeWindowOpen_ || undoStack_.empty()) {
            undoStack_.push_back(std::move(command));
            redoStack_.clear();
            while (undoStack_.size() > maxHistory_) {
                undoStack_.pop_front();
            }
            mergeWindowOpen_ = true;
            lastExecuteTime_ = now;
            return;
        }
        if (undoStack_.back()->mergeWith(*command)) {
            lastExecuteTime_ = now;
            return;
        }
        absorbIntoComposite(std::move(command));
        lastExecuteTime_ = now;
        return;
    }

    if (mergeWindowOpen_ && !undoStack_.empty() && now - lastExecuteTime_ <= mergeWindow_) {
        if (undoStack_.back()->mergeWith(*command)) {
            lastExecuteTime_ = now;
            return;
        }
    }

    undoStack_.push_back(std::move(command));
    redoStack_.clear();
    while (undoStack_.size() > maxHistory_) {
        undoStack_.pop_front();
    }
    mergeWindowOpen_ = true;
    lastExecuteTime_ = now;
}

void UndoManager::undo(Document &document) {
    if (undoStack_.empty()) {
        return;
    }
    mergeWindowOpen_ = false;
    packMergesIntoComposite_ = false;
    std::unique_ptr<Command> command = std::move(undoStack_.back());
    undoStack_.pop_back();
    command->undo(document);
    redoStack_.push_back(std::move(command));
}

void UndoManager::redo(Document &document) {
    if (redoStack_.empty()) {
        return;
    }
    mergeWindowOpen_ = false;
    packMergesIntoComposite_ = false;
    std::unique_ptr<Command> command = std::move(redoStack_.back());
    redoStack_.pop_back();
    command->execute(document);
    undoStack_.push_back(std::move(command));
}

bool UndoManager::canUndo() const {
    return !undoStack_.empty();
}

bool UndoManager::canRedo() const {
    return !redoStack_.empty();
}

std::string UndoManager::undoDescription() const {
    return undoStack_.empty() ? std::string{} : undoStack_.back()->describe();
}

std::string UndoManager::redoDescription() const {
    return redoStack_.empty() ? std::string{} : redoStack_.back()->describe();
}

void UndoManager::beginMergeGroup() {
    // Close any timed merge window so the next command starts a new unit.
    mergeWindowOpen_ = false;
    packMergesIntoComposite_ = true;
    lastExecuteTime_ = std::chrono::steady_clock::now();
}

void UndoManager::endMergeGroup() {
    mergeWindowOpen_ = false;
    packMergesIntoComposite_ = false;
}

void UndoManager::clear() {
    undoStack_.clear();
    redoStack_.clear();
    mergeWindowOpen_ = false;
    packMergesIntoComposite_ = false;
}

}  // namespace motion
