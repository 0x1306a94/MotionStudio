#include "MotionStudio/undo/SetStrokeCapCommand.h"

#include "FindStrokeStyle.h"

namespace motion {

SetStrokeCapCommand::SetStrokeCapCommand(EntityId layerId, int index, LineCap cap)
    : layerId_(layerId)
    , index_(index)
    , cap_(cap) {
}

void SetStrokeCapCommand::execute(Document &document) {
    StrokeStyle *stroke = FindStroke(document, layerId_, index_);
    if (stroke == nullptr) {
        return;
    }
    if (!oldCap_) {
        oldCap_ = stroke->cap;
    }
    stroke->cap = cap_;
}

void SetStrokeCapCommand::undo(Document &document) {
    if (!oldCap_) {
        return;
    }
    StrokeStyle *stroke = FindStroke(document, layerId_, index_);
    if (stroke != nullptr) {
        stroke->cap = *oldCap_;
    }
}

bool SetStrokeCapCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetStrokeCap) {
        return false;
    }
    const auto &typed = static_cast<const SetStrokeCapCommand &>(other);
    if (typed.layerId_ != layerId_ || typed.index_ != index_) {
        return false;
    }
    cap_ = typed.cap_;
    return true;
}

CommandKind SetStrokeCapCommand::kind() const {
    return CommandKind::SetStrokeCap;
}

std::string SetStrokeCapCommand::describe() const {
    return "Set Stroke Cap";
}

}  // namespace motion
