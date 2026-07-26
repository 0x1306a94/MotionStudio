#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "MotionStudio/undo/Command.h"

namespace motion {

class Document;

// Dual-stack undo/redo with a merge window.
// execute() behavior: runs the command → if the top-of-stack command is within
// the merge window and mergeWith succeeds, absorbs it (no push; typical for
// drags) → otherwise pushes onto the undo stack and clears the redo stack →
// drops the oldest entry when exceeding maxHistory.
//
// beginMergeGroup() opens an explicit drag transaction: subsequent commands
// that cannot merge by property are packed into one CompositeCommand until
// endMergeGroup(), so one UI undo undoes the whole drag (scale+position, etc.).
class UndoManager {
  public:
    // maxHistory: maximum number of commands kept in the undo stack.
    // mergeWindow: time window within which consecutive commands may merge.
    explicit UndoManager(size_t maxHistory = 200,
                         std::chrono::milliseconds mergeWindow = std::chrono::milliseconds(500));

    // Executes a command and records it for undo.
    // document: the document to mutate.
    // command: takes ownership of the command.
    void execute(Document &document, std::unique_ptr<Command> command);
    void undo(Document &document);
    void redo(Document &document);

    bool canUndo() const;
    bool canRedo() const;
    std::string undoDescription() const;
    std::string redoDescription() const;

    // Opens a drag transaction that packs mixed commands into one undo unit.
    void beginMergeGroup();
    // Call on mouse-up to close the merge window / drag transaction.
    void endMergeGroup();
    // Clears all history (undo history is not persisted).
    void clear();

  private:
    void absorbIntoComposite(std::unique_ptr<Command> command);

    std::deque<std::unique_ptr<Command>> undoStack_;
    std::vector<std::unique_ptr<Command>> redoStack_;
    size_t maxHistory_;
    std::chrono::milliseconds mergeWindow_;
    bool mergeWindowOpen_ = false;
    bool packMergesIntoComposite_ = false;
    std::chrono::steady_clock::time_point lastExecuteTime_{};
};

}  // namespace motion
