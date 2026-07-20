#include "MotionStudio/undo/UndoManager.h"

#include <utility>

#include "MotionStudio/model/Document.h"

namespace motion {

UndoManager::UndoManager(size_t maxHistory, std::chrono::milliseconds mergeWindow)
    : maxHistory_(maxHistory), mergeWindow_(mergeWindow) {}

void UndoManager::execute(Document& document, std::unique_ptr<Command> command) {
    command->execute(document);

    const auto now = std::chrono::steady_clock::now();
    if (mergeWindowOpen_ && !undoStack_.empty() &&
        now - lastExecuteTime_ <= mergeWindow_) {
        if (undoStack_.back()->mergeWith(*command)) {
            lastExecuteTime_ = now;  // 吸收，不压栈
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

void UndoManager::undo(Document& document) {
    if (undoStack_.empty()) {
        return;
    }
    mergeWindowOpen_ = false;
    std::unique_ptr<Command> command = std::move(undoStack_.back());
    undoStack_.pop_back();
    command->undo(document);
    redoStack_.push_back(std::move(command));
}

void UndoManager::redo(Document& document) {
    if (redoStack_.empty()) {
        return;
    }
    mergeWindowOpen_ = false;
    std::unique_ptr<Command> command = std::move(redoStack_.back());
    redoStack_.pop_back();
    command->execute(document);
    undoStack_.push_back(std::move(command));
}

std::string UndoManager::undoDescription() const {
    return undoStack_.empty() ? std::string{} : undoStack_.back()->describe();
}

std::string UndoManager::redoDescription() const {
    return redoStack_.empty() ? std::string{} : redoStack_.back()->describe();
}

void UndoManager::endMergeGroup() { mergeWindowOpen_ = false; }

void UndoManager::clear() {
    undoStack_.clear();
    redoStack_.clear();
    mergeWindowOpen_ = false;
}

}  // namespace motion
