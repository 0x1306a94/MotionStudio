#pragma once

#include <string>

#include "MotionStudio/undo/CommandKind.h"

namespace motion {

class Document;

// Command pattern: each UI edit produces a command.
// Commands hold only EntityId (not raw pointers). At undo time the target may
// have been deleted — it is resolved through EntityIndex and silently skipped
// if absent.
class Command {
  public:
    virtual ~Command() = default;

    virtual void execute(Document &document) = 0;  // first execution + redo
    virtual void undo(Document &document) = 0;

    // Concrete type tag used by mergeWith to type-check `other` before
    // downcasting (dynamic_cast is banned).
    virtual CommandKind kind() const = 0;

    // Merges consecutive operations (e.g. dozens of drag-induced commands)
    // into a single undo unit. On success, absorbs other (mutates self)
    // and the caller discards other.
    virtual bool mergeWith(const Command & /*other*/) {
        return false;
    }

    virtual std::string describe() const = 0;  // "Move Keyframe"
};

}  // namespace motion
